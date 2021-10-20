#include "xplane_adapter.h"

#include <QTimer>
#include <QtEndian>
#include <QDateTime>
#include <QJsonObject>
#include <QJsonDocument>

enum DataRef
{
    AvionicsPower,
    AudioComSelection,
    Com1AudioSelection,
    Com2AudioSelection,
    Com1Frequency,
    Com2Frequency,
    TransponderMode,
    TransponderIdent,
    TransponderCode,
    BeaconLights,
    LandingLights,
    TaxiLights,
    NavLights,
    StrobeLights,
    Latitude,
    Longitude,
    AltitudeMsl,
    AltitudeAgl,
    GroundSpeed,
    Pitch,
    Heading,
    Bank,
    LatitudeVelocity,
    AltitudeVelocity,
    LongitudeVelocity,
    PitchVelocity,
    HeadingVelocity,
    BankVelocity,
    EngineCount,
    OnGround,
    GearDown,
    FlapRatio,
    SpeedbrakeRatio,
    ReplayMode,
    PushToTalk
};

XplaneAdapter::XplaneAdapter(QObject* parent) : QObject(parent)
{
    m_zmqContext = new zmq::context_t(1);
    m_zmqSocket = new zmq::socket_t(*m_zmqContext, ZMQ_DEALER);
    m_zmqSocket->set(zmq::sockopt::routing_id, "xpilot");
    m_zmqSocket->set(zmq::sockopt::linger, 0);
    m_zmqSocket->connect("tcp://localhost:53100");

    m_zmqThread = new std::thread([&]{

    });

    socket = new QUdpSocket(this);
    connect(socket, &QUdpSocket::readyRead, this, &XplaneAdapter::OnDataReceived);

    QTimer *heartbeatTimer = new QTimer(this);
    connect(heartbeatTimer, &QTimer::timeout, this, [=] {
        qint64 now = QDateTime::currentSecsSinceEpoch();
        if((now - m_lastUdpTimestamp) > 5) {
            emit simConnectionStateChanged(false);
            m_simConnected = false;
            m_radioStackState = {};
            m_userAircraftData = {};
            m_userAircraftConfigData = {};
            Subscribe();
        } else {
            if(!m_simConnected) {
                emit simConnectionStateChanged(true);
                m_simConnected = true;
            }
        }
    });
    heartbeatTimer->start(1000);

    QTimer *xplaneDataTimer = new QTimer(this);
    connect(xplaneDataTimer, &QTimer::timeout, this, [=]{
        emit userAircraftDataChanged(m_userAircraftData);
        emit userAircraftConfigDataChanged(m_userAircraftConfigData);
        emit radioStackStateChanged(m_radioStackState);
    });
    xplaneDataTimer->start(50);

    Subscribe();
}

void XplaneAdapter::Subscribe()
{
    SubscribeDataRef("sim/cockpit2/switches/avionics_power_on", DataRef::AvionicsPower, 5);
    SubscribeDataRef("sim/cockpit2/radios/actuators/audio_com_selection", DataRef::AudioComSelection, 5);
    SubscribeDataRef("sim/cockpit2/radios/actuators/audio_selection_com1", DataRef::Com1AudioSelection, 5);
    SubscribeDataRef("sim/cockpit2/radios/actuators/audio_selection_com2", DataRef::Com2AudioSelection, 5);
    SubscribeDataRef("sim/cockpit2/radios/actuators/com1_frequency_hz_833", DataRef::Com1Frequency, 5);
    SubscribeDataRef("sim/cockpit2/radios/actuators/com2_frequency_hz_833", DataRef::Com2Frequency, 5);
    SubscribeDataRef("sim/cockpit/radios/transponder_mode", DataRef::TransponderMode, 5);
    SubscribeDataRef("sim/cockpit/radios/transponder_id", DataRef::TransponderIdent, 5);
    SubscribeDataRef("sim/cockpit/radios/transponder_code", DataRef::TransponderCode, 5);
    SubscribeDataRef("sim/cockpit2/switches/beacon_on", DataRef::BeaconLights, 5);
    SubscribeDataRef("sim/cockpit2/switches/landing_lights_on", DataRef::LandingLights, 5);
    SubscribeDataRef("sim/cockpit2/switches/taxi_light_on", DataRef::TaxiLights, 5);
    SubscribeDataRef("sim/cockpit2/switches/navigation_lights_on", DataRef::NavLights, 5);
    SubscribeDataRef("sim/cockpit2/switches/strobe_lights_on", DataRef::StrobeLights, 5);
    SubscribeDataRef("sim/flightmodel/position/latitude", DataRef::Latitude, 5);
    SubscribeDataRef("sim/flightmodel/position/longitude", DataRef::Longitude, 5);
    SubscribeDataRef("sim/flightmodel/position/elevation", DataRef::AltitudeMsl, 5);
    SubscribeDataRef("sim/flightmodel/position/y_agl", DataRef::AltitudeAgl, 5);
    SubscribeDataRef("sim/flightmodel/position/theta", DataRef::Pitch, 5);
    SubscribeDataRef("sim/flightmodel/position/psi", DataRef::Heading, 5);
    SubscribeDataRef("sim/flightmodel/position/phi", DataRef::Bank, 5);
    SubscribeDataRef("sim/flightmodel/position/local_vx", DataRef::LatitudeVelocity, 5);
    SubscribeDataRef("sim/flightmodel/position/local_vy", DataRef::AltitudeVelocity, 5);
    SubscribeDataRef("sim/flightmodel/position/local_vz", DataRef::LongitudeVelocity, 5);
    SubscribeDataRef("sim/flightmodel/position/Qrad", DataRef::PitchVelocity, 5);
    SubscribeDataRef("sim/flightmodel/position/Rrad", DataRef::HeadingVelocity, 5);
    SubscribeDataRef("sim/flightmodel/position/Prad", DataRef::BankVelocity, 5);
    SubscribeDataRef("sim/flightmodel/position/groundspeed", DataRef::GroundSpeed, 5);
    SubscribeDataRef("sim/aircraft/engine/acf_num_engines", DataRef::EngineCount, 5);
    SubscribeDataRef("sim/flightmodel/failures/onground_any", DataRef::OnGround, 5);
    SubscribeDataRef("sim/cockpit/switches/gear_handle_status", DataRef::GearDown, 5);
    SubscribeDataRef("sim/flightmodel/controls/flaprat", DataRef::FlapRatio, 5);
    SubscribeDataRef("sim/cockpit2/controls/speedbrake_ratio", DataRef::SpeedbrakeRatio, 5);
    SubscribeDataRef("sim/operation/prefs/replay_mode", DataRef::ReplayMode, 5);
    SubscribeDataRef("xpilot/ptt", DataRef::PushToTalk, 15);
}

void XplaneAdapter::SubscribeDataRef(std::string dataRef, uint32_t id, uint32_t frequency)
{
    QByteArray data;

    data.fill(0, 413);
    data.insert(0, "RREF");
    data.insert(5, (const char*)&frequency);
    data.insert(9, (const char*)&id);
    data.insert(13, dataRef.c_str());
    data.resize(413);

    socket->writeDatagram(data.data(), data.size(), QHostAddress::LocalHost, 49000);
}

void XplaneAdapter::setDataRefValue(std::string dataRef, float value)
{
    QByteArray data;

    data.fill(0, 509);
    data.insert(0, "DREF");
    data.insert(5, QByteArray::fromRawData(reinterpret_cast<char*>(&value), sizeof(float)));
    data.insert(9, dataRef.c_str());
    data.resize(509);

    socket->writeDatagram(data.data(), data.size(), QHostAddress::LocalHost, 49000);
}

void XplaneAdapter::sendCommand(std::string command)
{
    QByteArray data;

    data.fill(0, command.length() + 6);
    data.insert(0, "CMND");
    data.insert(5, command.c_str());
    data.resize(command.length() + 6);

    socket->writeDatagram(data.data(), data.size(), QHostAddress::LocalHost, 49000);
}

void XplaneAdapter::setTransponderCode(int code)
{
    setDataRefValue("sim/cockpit2/radios/actuators/transponder_code", code);
}

void XplaneAdapter::setCom1Frequency(float freq)
{
    setDataRefValue("sim/cockpit2/radios/actuators/com1_frequency_hz_833", freq);
}

void XplaneAdapter::setCom2Frequency(float freq)
{
    setDataRefValue("sim/cockpit2/radios/actuators/com2_frequency_hz_833", freq);
}

void XplaneAdapter::transponderIdent()
{
    sendCommand("sim/transponder/transponder_ident");
}

void XplaneAdapter::transponderModeToggle()
{
    if(m_radioStackState.SquawkingModeC) {
        setDataRefValue("sim/cockpit/radios/transponder_mode", 0);
    } else {
        setDataRefValue("sim/cockpit/radios/transponder_mode", 2);
    }
}

void XplaneAdapter::OnDataReceived()
{
    while(socket->hasPendingDatagrams())
    {
        QByteArray buffer;

        buffer.resize(socket->pendingDatagramSize());
        socket->readDatagram(buffer.data(), buffer.size());

        int pos = 0;
        QString header = QString::fromUtf8(buffer.mid(pos, 4));
        pos += 5;

        if(header == "RREF")
        {
            m_lastUdpTimestamp = QDateTime::currentSecsSinceEpoch();

            while(pos < buffer.length())
            {
                int id = *(reinterpret_cast<const int*>(buffer.mid(pos, 4).constData()));
                pos += 4;

                float value = *(reinterpret_cast<const float*>(buffer.mid(pos, 4).constData()));
                pos += 4;

                switch(id) {
                case DataRef::AvionicsPower:
                    m_radioStackState.AvionicsPowerOn = value;
                    break;
                case DataRef::AudioComSelection:
                    if(value == 6) {
                        m_radioStackState.Com1TransmitEnabled = true;
                        m_radioStackState.Com2TransmitEnabled = false;
                    } else if(value == 7) {
                        m_radioStackState.Com1TransmitEnabled = false;
                        m_radioStackState.Com2TransmitEnabled = true;
                    }
                    break;
                case DataRef::Com1AudioSelection:
                    m_radioStackState.Com1ReceiveEnabled = value;
                    break;
                case DataRef::Com2AudioSelection:
                    m_radioStackState.Com2ReceiveEnabled = value;
                    break;
                case DataRef::Com1Frequency:
                    m_radioStackState.Com1Frequency = value;
                    break;
                case DataRef::Com2Frequency:
                    m_radioStackState.Com2Frequency = value;
                    break;
                case DataRef::TransponderIdent:
                    m_radioStackState.SquawkingIdent = value;
                    break;
                case DataRef::TransponderMode:
                    m_radioStackState.SquawkingModeC = value >= 2;
                    break;
                case DataRef::TransponderCode:
                    m_radioStackState.TransponderCode = value;
                    break;
                case DataRef::Latitude:
                    m_userAircraftData.Latitude = value;
                    break;
                case DataRef::Longitude:
                    m_userAircraftData.Longitude = value;
                    break;
                case DataRef::Heading:
                    m_userAircraftData.Heading = value;
                    break;
                case DataRef::Pitch:
                    m_userAircraftData.Pitch = value;
                    break;
                case DataRef::Bank:
                    m_userAircraftData.Bank = value;
                    break;
                case DataRef::AltitudeMsl:
                    m_userAircraftData.AltitudeMslM = value;
                    break;
                case DataRef::AltitudeAgl:
                    m_userAircraftData.AltitudeAglM = value;
                    break;
                case DataRef::LatitudeVelocity:
                    m_userAircraftData.LatitudeVelocity = value;
                    break;
                case DataRef::AltitudeVelocity:
                    m_userAircraftData.AltitudeVelocity = value;
                    break;
                case DataRef::LongitudeVelocity:
                    m_userAircraftData.LongitudeVelocity = value;
                    break;
                case DataRef::PitchVelocity:
                    m_userAircraftData.PitchVelocity = value;
                    break;
                case DataRef::HeadingVelocity:
                    m_userAircraftData.HeadingVelocity = value;
                    break;
                case DataRef::BankVelocity:
                    m_userAircraftData.BankVelocity = value;
                    break;
                case DataRef::GroundSpeed:
                    m_userAircraftData.GroundSpeed = value * 1.94384; // mps -> knots
                    break;
                case DataRef::BeaconLights:
                    m_userAircraftConfigData.BeaconOn = value;
                    break;
                case DataRef::LandingLights:
                    m_userAircraftConfigData.LandingLightsOn = value;
                    break;
                case DataRef::TaxiLights:
                    m_userAircraftConfigData.TaxiLightsOn = value;
                    break;
                case DataRef::NavLights:
                    m_userAircraftConfigData.NavLightsOn = value;
                    break;
                case DataRef::StrobeLights:
                    m_userAircraftConfigData.StrobesOn = value;
                    break;
                case DataRef::EngineCount:
                    m_userAircraftConfigData.EngineCount = value;
                    break;
                case DataRef::OnGround:
                    m_userAircraftConfigData.OnGround = value;
                    break;
                case DataRef::GearDown:
                    m_userAircraftConfigData.GearDown = value;
                    break;
                case DataRef::FlapRatio:
                    m_userAircraftConfigData.FlapsRatio = value;
                    break;
                case DataRef::SpeedbrakeRatio:
                    m_userAircraftConfigData.SpeedbrakeRatio = value;
                    break;
                case DataRef::ReplayMode:
                    if(value > 0) {
                        emit replayModeDetected();
                    }
                    break;
                case DataRef::PushToTalk:
                    (value > 0) ? emit pttPressed() : emit pttReleased();
                    break;
                }
            }
        }
    }
}

void XplaneAdapter::sendSocketMessage(const QString &message)
{
    if(message.isEmpty()) return;

    if(m_zmqSocket != nullptr)
    {
        zmq::message_t msg(message.size());
        std::memcpy(msg.data(), message.toStdString().data(), message.size());
        m_zmqSocket->send(msg, zmq::send_flags::none);
    }
}

void XplaneAdapter::AddPlaneToSimulator(const NetworkAircraft &aircraft)
{
    QJsonObject reply;
    reply.insert("type", "AddPlane");

    QJsonObject data;
    data.insert("callsign", aircraft.Callsign);
    data.insert("airline", aircraft.Airline);
    data.insert("type_code", aircraft.TypeCode);
    data.insert("latitude", aircraft.RemoteVisualState.Latitude);
    data.insert("longitude", aircraft.RemoteVisualState.Longitude);
    data.insert("altitude", aircraft.RemoteVisualState.Altitude);
    data.insert("heading", aircraft.RemoteVisualState.Heading);
    data.insert("bank", aircraft.RemoteVisualState.Bank);
    data.insert("pitch", aircraft.RemoteVisualState.Pitch);
    reply.insert("data", data);

    QJsonDocument doc(reply);
    sendSocketMessage(QString(doc.toJson(QJsonDocument::Compact)));
}

void XplaneAdapter::PlaneConfigChanged(const NetworkAircraft &aircraft)
{
    QJsonObject reply;
    reply.insert("type", "AirplaneConfig");

    QJsonObject data;
    data.insert("callsign", aircraft.Callsign);

    if(!aircraft.Configuration->IsIncremental())
    {
        data.insert("full_config", true);
    }

    if(aircraft.Configuration->Engines.has_value())
    {
        data.insert("engines_on", aircraft.Configuration->IsAnyEngineRunning());
    }

    if(aircraft.Configuration->OnGround.has_value())
    {
        data.insert("on_ground", aircraft.Configuration->OnGround.value());
    }

    if(aircraft.Configuration->FlapsPercent.has_value())
    {
        data.insert("flaps", aircraft.Configuration->FlapsPercent.value() / 100.0f);
    }

    if(aircraft.Configuration->GearDown.has_value())
    {
        data.insert("gear_down", aircraft.Configuration->GearDown.value());
    }

    if(aircraft.Configuration->Lights->HasLights())
    {
        QJsonObject lights;
        if(aircraft.Configuration->Lights->BeaconOn.has_value())
        {
            lights.insert("beacon_lights_on", aircraft.Configuration->Lights->BeaconOn.value());
        }
        if(aircraft.Configuration->Lights->LandingOn.has_value())
        {
            lights.insert("landing_lights_on", aircraft.Configuration->Lights->LandingOn.value());
        }
        if(aircraft.Configuration->Lights->NavOn.has_value())
        {
            lights.insert("nav_lights_on", aircraft.Configuration->Lights->NavOn.value());
        }
        if(aircraft.Configuration->Lights->StrobeOn.has_value())
        {
            lights.insert("strobe_lights_on", aircraft.Configuration->Lights->StrobeOn.value());
        }
        if(aircraft.Configuration->Lights->TaxiOn.has_value())
        {
            lights.insert("taxi_lights_on", aircraft.Configuration->Lights->TaxiOn.value());
        }
        data.insert("lights", lights);
    }

    reply.insert("data", data);
    QJsonDocument doc(reply);
    sendSocketMessage(QString(doc.toJson(QJsonDocument::Compact)));
}

void XplaneAdapter::PlaneModelChanged(const NetworkAircraft &aircraft)
{
    QJsonObject reply;
    reply.insert("type", "ChangeModel");

    QJsonObject data;
    data.insert("callsign", aircraft.Callsign);
    data.insert("type_code", aircraft.TypeCode);
    data.insert("airline", aircraft.Airline);

    reply.insert("data", data);
    QJsonDocument doc(reply);
    sendSocketMessage(QString(doc.toJson(QJsonDocument::Compact)));
}

void XplaneAdapter::DeleteAircraft(const NetworkAircraft &aircraft)
{
    QJsonObject reply;
    reply.insert("type", "RemovePlane");

    QJsonObject data;
    data.insert("callsign", aircraft.Callsign);

    reply.insert("data", data);
    QJsonDocument doc(reply);
    sendSocketMessage(QString(doc.toJson(QJsonDocument::Compact)));
}

void XplaneAdapter::DeleteAllAircraft()
{
    QJsonObject reply;
    reply.insert("type", "RemoveAllPlanes");
    QJsonDocument doc(reply);
    sendSocketMessage(QString(doc.toJson(QJsonDocument::Compact)));
}

void XplaneAdapter::SendSlowPositionUpdate(const NetworkAircraft &aircraft, const AircraftVisualState &visualState, const double& groundSpeed)
{
    QJsonObject reply;
    reply.insert("type", "SlowPositionUpdate");

    QJsonObject data;
    data.insert("callsign", aircraft.Callsign);
    data.insert("latitude", visualState.Latitude);
    data.insert("longitude", visualState.Longitude);
    data.insert("altitude", visualState.Altitude);
    data.insert("heading", visualState.Heading);
    data.insert("bank", visualState.Bank);
    data.insert("pitch", visualState.Pitch);
    data.insert("ground_speed", groundSpeed);

    reply.insert("data", data);
    QJsonDocument doc(reply);
    sendSocketMessage(QString(doc.toJson(QJsonDocument::Compact)));
}

void XplaneAdapter::SendFastPositionUpdate(const NetworkAircraft &aircraft, const AircraftVisualState &visualState, const VelocityVector &positionalVelocityVector, const VelocityVector &rotationalVelocityVector)
{
    QJsonObject reply;
    reply.insert("type", "FastPositionUpdate");

    QJsonObject data;
    data.insert("callsign", aircraft.Callsign);
    data.insert("latitude", visualState.Latitude);
    data.insert("longitude", visualState.Longitude);
    data.insert("altitude", visualState.Altitude);
    data.insert("heading", visualState.Heading);
    data.insert("bank", visualState.Bank);
    data.insert("pitch", visualState.Pitch);
    data.insert("vx", positionalVelocityVector.X);
    data.insert("vy", positionalVelocityVector.Y);
    data.insert("vz", positionalVelocityVector.Z);
    data.insert("vp", rotationalVelocityVector.X);
    data.insert("vh", rotationalVelocityVector.Y);
    data.insert("vb", rotationalVelocityVector.Z);

    reply.insert("data", data);
    QJsonDocument doc(reply);
    sendSocketMessage(QString(doc.toJson(QJsonDocument::Compact)));
}