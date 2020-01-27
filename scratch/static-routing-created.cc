#include <iostream>
#include <fstream>
#include <string>
#include <cassert>
#include <stdlib.h>
#include <time.h>
#include <tuple>
#include <vector>
#include <array>

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/csma-module.h"
#include "ns3/applications-module.h"
#include "ns3/ipv4-static-routing-helper.h"
#include "ns3/netanim-module.h"
#include "ns3/traffic-control-helper.h"


#define PACKET_SIZE 1300 //bytes 分割・統合されないサイズにする
#define SEGMENT_SIZE 1300 //bytes この大きさのデータがたまると送信される
#define ONE_DATUM 100 //パケットで1データ
#define DEFAULT_SEND_RATE "0.05Mbps"
#define NUM_PACKETS 30000
#define END_TIME 41 //Seconds
#define INTERVAL 20 //Seconds
#define TCP_TYPE "ns3::TcpNewReno"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE ("Multipath staticrouging");

Ptr<OutputStreamWrapper> streamLinkTrafSize;
Ptr<OutputStreamWrapper> streamLinkPktCount;
Ptr<OutputStreamWrapper> streamLinkLossCount;

class MyApp : public Application 
{
  public:
    MyApp ();
    virtual ~MyApp();
    void Setup (TypeId tid,Ptr<Node> node, Address address, uint32_t packetSize, uint32_t nPackets, DataRate dataRate, std::string name);
    void ChangeDataRate(double);
    void DetectPacketLoss (const uint32_t, const uint32_t);
    void CountTCPTx (const Ptr<const Packet> packet, const TcpHeader &header, const Ptr<const TcpSocketBase> socket);
    void woTCPTx (double time);

  private:
    virtual void StartApplication (void);
    virtual void StopApplication (void);

    void ScheduleTx (void);
    void SendPacket (void);
    void ReConnect (void);
    TypeId      m_tid;
    Ptr<Node>   m_node;
    Ptr<Socket> m_socket;
    Address     m_peer;
    uint32_t    m_packetSize;
    uint32_t    m_nPackets;
    DataRate    m_dataRate;
    EventId     m_sendEvent;
    bool        m_running;
    uint32_t    m_packetsSent;
    std::string m_name;
    uint32_t    m_tcpsent;
    uint32_t    m_tcpsentSize;
    uint32_t    m_tcpsentCount;
    uint32_t    m_packetLoss;
    uint32_t    m_packetLossParTime;
    uint64_t    m_targetRate;
    Ptr<OutputStreamWrapper> m_cwndStream;
    Ptr<OutputStreamWrapper> m_datarateStream;
    Ptr<OutputStreamWrapper> m_lossStream;
    Ptr<OutputStreamWrapper> m_tcpTxStream;

};

MyApp::MyApp ()
  : m_tid (),
    m_node(),
    m_socket (),
    m_peer (), 
    m_packetSize (0), 
    m_nPackets (0), 
    m_dataRate (0), 
    m_sendEvent (), 
    m_running (false), 
    m_packetsSent (0),
    m_name (""),
    m_tcpsent (0),
    m_tcpsentSize (0),
    m_tcpsentCount (0),
    m_packetLoss (0),
    m_packetLossParTime (0),
    m_targetRate (0),
    m_cwndStream (),
    m_datarateStream (),
    m_lossStream (),
    m_tcpTxStream ()
{
}

MyApp::~MyApp()
{
  m_socket = 0;
}

void
MyApp::Setup (TypeId tid,Ptr<Node> node, Address address, uint32_t packetSize, uint32_t nPackets, DataRate dataRate, std::string name)
{
  m_tid = tid;
  m_node = node;
  m_socket = Socket::CreateSocket (m_node, m_tid);
  m_peer = address;
  m_packetSize = packetSize;
  m_nPackets = nPackets;
  m_dataRate = dataRate;
  m_name = name;
  m_targetRate = dataRate.GetBitRate ();

  AsciiTraceHelper ascii;
  m_cwndStream = ascii.CreateFileStream ("./Plot/Data/"+m_name+".cwnd");
  m_datarateStream = ascii.CreateFileStream ("./Plot/Data/"+m_name+".drate");
  m_lossStream = ascii.CreateFileStream ("./Plot/Data/"+m_name+".loss");
  m_tcpTxStream = ascii.CreateFileStream ("./Plot/Data/"+m_name+".thr");
}

void
MyApp::StartApplication (void)
{
  m_running = true;
  m_packetsSent = 0;
  m_socket->Bind ();
  m_socket->Connect (m_peer);
  m_socket->TraceConnectWithoutContext("Tx", MakeCallback (&MyApp::CountTCPTx, this));
  m_socket->TraceConnectWithoutContext("CongestionWindow", MakeCallback (&MyApp::DetectPacketLoss, this));
  SendPacket ();
  woTCPTx (1);
}

void 
MyApp::StopApplication (void)
{
  m_running = false;

  if (m_sendEvent.IsRunning ())
    {
      Simulator::Cancel (m_sendEvent);
    }

  if (m_socket)
    {
      m_socket->Close ();
    }

  m_socket = 0;
}

void
MyApp::ReConnect (void)
{
  m_packetLoss = 0;
  m_tcpsent = 0;
  m_running = true;
  m_socket = Socket::CreateSocket (m_node, m_tid);;
  m_socket->Bind ();
  m_socket->Connect (m_peer);
  m_socket->TraceConnectWithoutContext("Tx", MakeCallback (&MyApp::CountTCPTx, this));
  m_socket->TraceConnectWithoutContext("CongestionWindow", MakeCallback (&MyApp::DetectPacketLoss, this));
  SendPacket ();
}

void
MyApp::SendPacket (void)
{
  Ptr<Packet> packet = Create<Packet> (m_packetSize);
  m_socket->Send (packet);

  if(++m_packetsSent % ONE_DATUM == 0)   // １データ送信でコネクション終了
  {
    StopApplication ();
    double lossRate = m_packetLoss / (double) m_tcpsent;
    ChangeDataRate (lossRate);

    // Trace datarate, lossrate
    *m_datarateStream->GetStream () << Simulator::Now ().GetSeconds () << " " << m_dataRate.GetBitRate () << std::endl;
    
    if (m_packetsSent < m_nPackets)
    {
        Simulator::ScheduleNow (&MyApp::ReConnect,this);
    }
  }

  if (m_packetsSent < m_nPackets)
  {
    ScheduleTx ();
  }
}

void
MyApp::ScheduleTx (void)
{
  if (m_running)
    {
      Time tNext (Seconds (m_packetSize * 8 / static_cast<double> (m_dataRate.GetBitRate ())));
      m_sendEvent = Simulator::Schedule (tNext, &MyApp::SendPacket, this);
    }
}

void
MyApp::ChangeDataRate (double lossRate)
{
  m_dataRate =  DataRate(static_cast<uint64_t>(m_targetRate * exp (-13.1 * lossRate)));
}

void
MyApp::DetectPacketLoss (const uint32_t org, const uint32_t cgd)
{
  // *m_cwndStream->GetStream () << Simulator::Now ().GetSeconds () << " " << cgd << std::endl;
  if(org > cgd) //cwnd 減少
  {
    ++m_packetLoss;
    ++m_packetLossParTime;
  }
}

void
MyApp::CountTCPTx (const Ptr<const Packet> packet, const TcpHeader &header, const Ptr<const TcpSocketBase> socket)
{
  if(packet->GetSize () > 0) 
  {
    ++m_tcpsent;
    ++m_tcpsentCount;
    m_tcpsentSize += packet->GetSize () * 8;
  }
}

void
MyApp::woTCPTx (double time)
{
  *m_tcpTxStream->GetStream () << Simulator::Now ().GetSeconds () << " " << m_tcpsentSize / time << std::endl;
  *m_lossStream->GetStream () << Simulator::Now ().GetSeconds () << " " << m_packetLossParTime / (double) m_tcpsentCount  << std::endl;
  m_tcpsentSize = 0;
  m_tcpsentCount = 0;
  m_packetLossParTime = 0;
  Simulator::Schedule (Time ( Seconds (time)), &MyApp::woTCPTx, this, time);
}

std::array<uint64_t, 28> pktCountAry = {0};
std::array<uint64_t, 28> pktSizeCountAry = {0};
static void
linkPktCount (uint16_t linkn, Ptr< const Packet > packet)
{
  pktCountAry[linkn] += 1;
  pktSizeCountAry[linkn] += packet->GetSize ();
}

std::array<uint64_t, 28> pktLossCountAry = {0};
static void
linkPktLossCount (uint16_t const linkn, Ptr<ns3::QueueDiscItem const> item)
{
  pktLossCountAry[linkn] += 1;
}

static void
monitorLink (double time)
{
  for (uint8_t i = 0; i < 28; i++)
  {
    *streamLinkTrafSize->GetStream () << pktSizeCountAry[i] << std::endl;
    *streamLinkPktCount->GetStream () << pktCountAry[i] << std::endl;
    *streamLinkLossCount->GetStream () << pktLossCountAry[i] << std::endl;
  }
  *streamLinkTrafSize->GetStream ()<< std::endl;
  *streamLinkPktCount->GetStream ()<< std::endl;
  *streamLinkLossCount->GetStream ()<< std::endl;
  
  pktSizeCountAry = {0};
  pktCountAry = {0};
  pktLossCountAry = {0};

  Simulator::Schedule (Time ( Seconds (time)), &monitorLink, time);
}


typedef std::tuple<
  std::vector <int>,
  std::vector <double>
> rvector;

int 
main (int argc, char *argv[])
{
  CommandLine cmd;
  cmd.Parse (argc, argv);

  // Set default tcp type
  Config::SetDefault ("ns3::TcpL4Protocol::SocketType", StringValue (TCP_TYPE));
  // Set default tcp segment size
  Config::SetDefault ("ns3::TcpSocket::SegmentSize", UintegerValue (SEGMENT_SIZE)); 

  srand((unsigned)time(NULL));

/////////////////////////////////////
  NodeContainer c,c_e;
  c.Create (11);
  c_e.Create (11);

  InternetStackHelper internet;
  internet.Install (c);
  internet.Install (c_e);

  NodeContainer nAB = NodeContainer (c.Get (0), c.Get (1));
  NodeContainer nAC = NodeContainer (c.Get (0), c.Get (2));
  NodeContainer nBC = NodeContainer (c.Get (1), c.Get (2));
  NodeContainer nBD = NodeContainer (c.Get (1), c.Get (3));
  NodeContainer nCE = NodeContainer (c.Get (2), c.Get (4));
  NodeContainer nEF = NodeContainer (c.Get (4), c.Get (5));
  NodeContainer nEH = NodeContainer (c.Get (4), c.Get (7));
  NodeContainer nDF = NodeContainer (c.Get (3), c.Get (5));
  NodeContainer nFI = NodeContainer (c.Get (5), c.Get (8));
  NodeContainer nGH = NodeContainer (c.Get (6), c.Get (7));
  NodeContainer nHI = NodeContainer (c.Get (7), c.Get (8));
  NodeContainer nGK = NodeContainer (c.Get (6), c.Get (10));
  NodeContainer nIJ = NodeContainer (c.Get (8), c.Get (9));
  NodeContainer nJK = NodeContainer (c.Get (9), c.Get (10));

  NodeContainer nAAe = NodeContainer (c.Get (0), c_e.Get (0));
  NodeContainer nBBe = NodeContainer (c.Get (1), c_e.Get (1));
  NodeContainer nCCe = NodeContainer (c.Get (2), c_e.Get (2));
  NodeContainer nDDe = NodeContainer (c.Get (3), c_e.Get (3));
  NodeContainer nEEe = NodeContainer (c.Get (4), c_e.Get (4));
  NodeContainer nFFe = NodeContainer (c.Get (5), c_e.Get (5));
  NodeContainer nGGe = NodeContainer (c.Get (6), c_e.Get (6));
  NodeContainer nHHe = NodeContainer (c.Get (7), c_e.Get (7));
  NodeContainer nIIe = NodeContainer (c.Get (8), c_e.Get (8));
  NodeContainer nJJe = NodeContainer (c.Get (9), c_e.Get (9));
  NodeContainer nKKe = NodeContainer (c.Get (10), c_e.Get (10));

  PointToPointHelper p2p, p2p_l;
  p2p.SetChannelAttribute ("Delay", StringValue ("1ms"));
  p2p_l.SetDeviceAttribute ("DataRate", StringValue ("100Mbps"));
  // p2p_l.SetDeviceAttribute ("Delay", StringValue ("1ms"));
  NetDeviceContainer dAB = p2p.Install (nAB);
  NetDeviceContainer dAC = p2p.Install (nAC);
  NetDeviceContainer dBC = p2p.Install (nBC);
  NetDeviceContainer dBD = p2p.Install (nBD);
  NetDeviceContainer dCE = p2p.Install (nCE);
  NetDeviceContainer dEF = p2p.Install (nEF);
  NetDeviceContainer dEH = p2p.Install (nEH);
  NetDeviceContainer dDF = p2p.Install (nDF);
  NetDeviceContainer dFI = p2p.Install (nFI);
  NetDeviceContainer dGH = p2p.Install (nGH);
  NetDeviceContainer dHI = p2p.Install (nHI);
  NetDeviceContainer dGK = p2p.Install (nGK);
  NetDeviceContainer dIJ = p2p.Install (nIJ);
  NetDeviceContainer dJK = p2p.Install (nJK);

  NetDeviceContainer dAAe = p2p_l.Install (nAAe);
  NetDeviceContainer dBBe = p2p_l.Install (nBBe);
  NetDeviceContainer dCCe = p2p_l.Install (nCCe);
  NetDeviceContainer dDDe = p2p_l.Install (nDDe);
  NetDeviceContainer dEEe = p2p_l.Install (nEEe);
  NetDeviceContainer dFFe = p2p_l.Install (nFFe);
  NetDeviceContainer dGGe = p2p_l.Install (nGGe);
  NetDeviceContainer dHHe = p2p_l.Install (nHHe);
  NetDeviceContainer dIIe = p2p_l.Install (nIIe);
  NetDeviceContainer dJJe = p2p_l.Install (nJJe);
  NetDeviceContainer dKKe = p2p_l.Install (nKKe);

  Config::Set("/NodeList/0/$ns3::Node/DeviceList/1/$ns3::PointToPointNetDevice/DataRate", DataRateValue (DataRate("30Mbps")));
  Config::Set("/NodeList/0/$ns3::Node/DeviceList/2/$ns3::PointToPointNetDevice/DataRate", DataRateValue (DataRate("30Mbps")));
  Config::Set("/NodeList/1/$ns3::Node/DeviceList/1/$ns3::PointToPointNetDevice/DataRate", DataRateValue (DataRate("24Mbps")));
  Config::Set("/NodeList/1/$ns3::Node/DeviceList/2/$ns3::PointToPointNetDevice/DataRate", DataRateValue (DataRate("36Mbps")));
  Config::Set("/NodeList/1/$ns3::Node/DeviceList/3/$ns3::PointToPointNetDevice/DataRate", DataRateValue (DataRate("54Mbps")));
  Config::Set("/NodeList/2/$ns3::Node/DeviceList/1/$ns3::PointToPointNetDevice/DataRate", DataRateValue (DataRate("36Mbps")));
  Config::Set("/NodeList/2/$ns3::Node/DeviceList/2/$ns3::PointToPointNetDevice/DataRate", DataRateValue (DataRate("30Mbps")));
  Config::Set("/NodeList/2/$ns3::Node/DeviceList/3/$ns3::PointToPointNetDevice/DataRate", DataRateValue (DataRate("90Mbps")));
  Config::Set("/NodeList/3/$ns3::Node/DeviceList/1/$ns3::PointToPointNetDevice/DataRate", DataRateValue (DataRate("54Mbps")));
  Config::Set("/NodeList/3/$ns3::Node/DeviceList/2/$ns3::PointToPointNetDevice/DataRate", DataRateValue (DataRate("78Mbps")));
  Config::Set("/NodeList/4/$ns3::Node/DeviceList/1/$ns3::PointToPointNetDevice/DataRate", DataRateValue (DataRate("90Mbps")));
  Config::Set("/NodeList/4/$ns3::Node/DeviceList/2/$ns3::PointToPointNetDevice/DataRate", DataRateValue (DataRate("54Mbps")));
  Config::Set("/NodeList/4/$ns3::Node/DeviceList/3/$ns3::PointToPointNetDevice/DataRate", DataRateValue (DataRate("90Mbps")));
  Config::Set("/NodeList/5/$ns3::Node/DeviceList/1/$ns3::PointToPointNetDevice/DataRate", DataRateValue (DataRate("54Mbps")));
  Config::Set("/NodeList/5/$ns3::Node/DeviceList/2/$ns3::PointToPointNetDevice/DataRate", DataRateValue (DataRate("78Mbps")));
  Config::Set("/NodeList/5/$ns3::Node/DeviceList/3/$ns3::PointToPointNetDevice/DataRate", DataRateValue (DataRate("90Mbps")));
  Config::Set("/NodeList/6/$ns3::Node/DeviceList/1/$ns3::PointToPointNetDevice/DataRate", DataRateValue (DataRate("72Mbps")));
  Config::Set("/NodeList/6/$ns3::Node/DeviceList/2/$ns3::PointToPointNetDevice/DataRate", DataRateValue (DataRate("36Mbps")));
  Config::Set("/NodeList/7/$ns3::Node/DeviceList/1/$ns3::PointToPointNetDevice/DataRate", DataRateValue (DataRate("90Mbps")));
  Config::Set("/NodeList/7/$ns3::Node/DeviceList/2/$ns3::PointToPointNetDevice/DataRate", DataRateValue (DataRate("66Mbps")));
  Config::Set("/NodeList/7/$ns3::Node/DeviceList/3/$ns3::PointToPointNetDevice/DataRate", DataRateValue (DataRate("36Mbps")));
  Config::Set("/NodeList/8/$ns3::Node/DeviceList/1/$ns3::PointToPointNetDevice/DataRate", DataRateValue (DataRate("90Mbps")));
  Config::Set("/NodeList/8/$ns3::Node/DeviceList/2/$ns3::PointToPointNetDevice/DataRate", DataRateValue (DataRate("42Mbps")));
  Config::Set("/NodeList/8/$ns3::Node/DeviceList/3/$ns3::PointToPointNetDevice/DataRate", DataRateValue (DataRate("72Mbps")));
  Config::Set("/NodeList/9/$ns3::Node/DeviceList/1/$ns3::PointToPointNetDevice/DataRate", DataRateValue (DataRate("78Mbps")));
  Config::Set("/NodeList/9/$ns3::Node/DeviceList/2/$ns3::PointToPointNetDevice/DataRate", DataRateValue (DataRate("42Mbps")));
  Config::Set("/NodeList/10/$ns3::Node/DeviceList/1/$ns3::PointToPointNetDevice/DataRate", DataRateValue (DataRate("30Mbps")));
  Config::Set("/NodeList/10/$ns3::Node/DeviceList/2/$ns3::PointToPointNetDevice/DataRate", DataRateValue (DataRate("36Mbps")));

  TrafficControlHelper tch;
  tch.SetRootQueueDisc ("ns3::FqCoDelQueueDisc");
  tch.Install (dAB);
  tch.Install (dAC);
  tch.Install (dBC);
  tch.Install (dBD);
  tch.Install (dCE);
  tch.Install (dEF);
  tch.Install (dEH);
  tch.Install (dDF);
  tch.Install (dFI);
  tch.Install (dGH);
  tch.Install (dHI);
  tch.Install (dGK);
  tch.Install (dIJ);
  tch.Install (dJK);

  dAB.Get (0)->TraceConnectWithoutContext("PhyTxEnd", MakeBoundCallback(&linkPktCount, 0));
  dAC.Get (0)->TraceConnectWithoutContext("PhyTxEnd", MakeBoundCallback(&linkPktCount, 1));
  dAB.Get (1)->TraceConnectWithoutContext("PhyTxEnd", MakeBoundCallback(&linkPktCount, 2));
  dBC.Get (0)->TraceConnectWithoutContext("PhyTxEnd", MakeBoundCallback(&linkPktCount, 3));
  dBD.Get (0)->TraceConnectWithoutContext("PhyTxEnd", MakeBoundCallback(&linkPktCount, 4));
  dAC.Get (1)->TraceConnectWithoutContext("PhyTxEnd", MakeBoundCallback(&linkPktCount, 5));
  dBC.Get (1)->TraceConnectWithoutContext("PhyTxEnd", MakeBoundCallback(&linkPktCount, 6));
  dCE.Get (0)->TraceConnectWithoutContext("PhyTxEnd", MakeBoundCallback(&linkPktCount, 7));
  dBD.Get (1)->TraceConnectWithoutContext("PhyTxEnd", MakeBoundCallback(&linkPktCount, 8));
  dDF.Get (0)->TraceConnectWithoutContext("PhyTxEnd", MakeBoundCallback(&linkPktCount, 9));
  dCE.Get (1)->TraceConnectWithoutContext("PhyTxEnd", MakeBoundCallback(&linkPktCount, 10));
  dEF.Get (0)->TraceConnectWithoutContext("PhyTxEnd", MakeBoundCallback(&linkPktCount, 11));
  dEH.Get (0)->TraceConnectWithoutContext("PhyTxEnd", MakeBoundCallback(&linkPktCount, 12));
  dEF.Get (1)->TraceConnectWithoutContext("PhyTxEnd", MakeBoundCallback(&linkPktCount, 13));
  dDF.Get (1)->TraceConnectWithoutContext("PhyTxEnd", MakeBoundCallback(&linkPktCount, 14));
  dFI.Get (0)->TraceConnectWithoutContext("PhyTxEnd", MakeBoundCallback(&linkPktCount, 15));
  dGH.Get (0)->TraceConnectWithoutContext("PhyTxEnd", MakeBoundCallback(&linkPktCount, 16));
  dGK.Get (0)->TraceConnectWithoutContext("PhyTxEnd", MakeBoundCallback(&linkPktCount, 17));
  dEH.Get (1)->TraceConnectWithoutContext("PhyTxEnd", MakeBoundCallback(&linkPktCount, 18));
  dGH.Get (1)->TraceConnectWithoutContext("PhyTxEnd", MakeBoundCallback(&linkPktCount, 19));
  dHI.Get (0)->TraceConnectWithoutContext("PhyTxEnd", MakeBoundCallback(&linkPktCount, 20));
  dFI.Get (1)->TraceConnectWithoutContext("PhyTxEnd", MakeBoundCallback(&linkPktCount, 21));
  dHI.Get (1)->TraceConnectWithoutContext("PhyTxEnd", MakeBoundCallback(&linkPktCount, 22));
  dIJ.Get (0)->TraceConnectWithoutContext("PhyTxEnd", MakeBoundCallback(&linkPktCount, 23));
  dIJ.Get (1)->TraceConnectWithoutContext("PhyTxEnd", MakeBoundCallback(&linkPktCount, 24));
  dJK.Get (0)->TraceConnectWithoutContext("PhyTxEnd", MakeBoundCallback(&linkPktCount, 25));
  dGK.Get (1)->TraceConnectWithoutContext("PhyTxEnd", MakeBoundCallback(&linkPktCount, 26));
  dJK.Get (1)->TraceConnectWithoutContext("PhyTxEnd", MakeBoundCallback(&linkPktCount, 27));

  Config::ConnectWithoutContext ("/NodeList/0/$ns3::TrafficControlLayer/RootQueueDiscList/1/Drop", MakeBoundCallback (&linkPktLossCount, 0));
  Config::ConnectWithoutContext ("/NodeList/0/$ns3::TrafficControlLayer/RootQueueDiscList/2/Drop", MakeBoundCallback (&linkPktLossCount, 1));
  Config::ConnectWithoutContext ("/NodeList/1/$ns3::TrafficControlLayer/RootQueueDiscList/1/Drop", MakeBoundCallback (&linkPktLossCount, 2));
  Config::ConnectWithoutContext ("/NodeList/1/$ns3::TrafficControlLayer/RootQueueDiscList/2/Drop", MakeBoundCallback (&linkPktLossCount, 3));
  Config::ConnectWithoutContext ("/NodeList/1/$ns3::TrafficControlLayer/RootQueueDiscList/3/Drop", MakeBoundCallback (&linkPktLossCount, 4));
  Config::ConnectWithoutContext ("/NodeList/2/$ns3::TrafficControlLayer/RootQueueDiscList/1/Drop", MakeBoundCallback (&linkPktLossCount, 5));
  Config::ConnectWithoutContext ("/NodeList/2/$ns3::TrafficControlLayer/RootQueueDiscList/2/Drop", MakeBoundCallback (&linkPktLossCount, 6));
  Config::ConnectWithoutContext ("/NodeList/2/$ns3::TrafficControlLayer/RootQueueDiscList/3/Drop", MakeBoundCallback (&linkPktLossCount, 7));
  Config::ConnectWithoutContext ("/NodeList/3/$ns3::TrafficControlLayer/RootQueueDiscList/1/Drop", MakeBoundCallback (&linkPktLossCount, 8));
  Config::ConnectWithoutContext ("/NodeList/3/$ns3::TrafficControlLayer/RootQueueDiscList/2/Drop", MakeBoundCallback (&linkPktLossCount, 9));
  Config::ConnectWithoutContext ("/NodeList/4/$ns3::TrafficControlLayer/RootQueueDiscList/1/Drop", MakeBoundCallback (&linkPktLossCount, 10));
  Config::ConnectWithoutContext ("/NodeList/4/$ns3::TrafficControlLayer/RootQueueDiscList/2/Drop", MakeBoundCallback (&linkPktLossCount, 11));
  Config::ConnectWithoutContext ("/NodeList/4/$ns3::TrafficControlLayer/RootQueueDiscList/3/Drop", MakeBoundCallback (&linkPktLossCount, 12));
  Config::ConnectWithoutContext ("/NodeList/5/$ns3::TrafficControlLayer/RootQueueDiscList/1/Drop", MakeBoundCallback (&linkPktLossCount, 13));
  Config::ConnectWithoutContext ("/NodeList/5/$ns3::TrafficControlLayer/RootQueueDiscList/2/Drop", MakeBoundCallback (&linkPktLossCount, 14));
  Config::ConnectWithoutContext ("/NodeList/5/$ns3::TrafficControlLayer/RootQueueDiscList/3/Drop", MakeBoundCallback (&linkPktLossCount, 15));
  Config::ConnectWithoutContext ("/NodeList/6/$ns3::TrafficControlLayer/RootQueueDiscList/1/Drop", MakeBoundCallback (&linkPktLossCount, 16));
  Config::ConnectWithoutContext ("/NodeList/6/$ns3::TrafficControlLayer/RootQueueDiscList/2/Drop", MakeBoundCallback (&linkPktLossCount, 17));
  Config::ConnectWithoutContext ("/NodeList/7/$ns3::TrafficControlLayer/RootQueueDiscList/1/Drop", MakeBoundCallback (&linkPktLossCount, 18));
  Config::ConnectWithoutContext ("/NodeList/7/$ns3::TrafficControlLayer/RootQueueDiscList/2/Drop", MakeBoundCallback (&linkPktLossCount, 19));
  Config::ConnectWithoutContext ("/NodeList/7/$ns3::TrafficControlLayer/RootQueueDiscList/3/Drop", MakeBoundCallback (&linkPktLossCount, 20));
  Config::ConnectWithoutContext ("/NodeList/8/$ns3::TrafficControlLayer/RootQueueDiscList/1/Drop", MakeBoundCallback (&linkPktLossCount, 21));
  Config::ConnectWithoutContext ("/NodeList/8/$ns3::TrafficControlLayer/RootQueueDiscList/2/Drop", MakeBoundCallback (&linkPktLossCount, 22));
  Config::ConnectWithoutContext ("/NodeList/8/$ns3::TrafficControlLayer/RootQueueDiscList/3/Drop", MakeBoundCallback (&linkPktLossCount, 23));
  Config::ConnectWithoutContext ("/NodeList/9/$ns3::TrafficControlLayer/RootQueueDiscList/1/Drop", MakeBoundCallback (&linkPktLossCount, 24));
  Config::ConnectWithoutContext ("/NodeList/9/$ns3::TrafficControlLayer/RootQueueDiscList/2/Drop", MakeBoundCallback (&linkPktLossCount, 25));
  Config::ConnectWithoutContext ("/NodeList/10/$ns3::TrafficControlLayer/RootQueueDiscList/1/Drop", MakeBoundCallback (&linkPktLossCount, 26));
  Config::ConnectWithoutContext ("/NodeList/10/$ns3::TrafficControlLayer/RootQueueDiscList/2/Drop", MakeBoundCallback (&linkPktLossCount, 27));

  Ipv4AddressHelper ipv4;
  ipv4.SetBase ("10.1.1.0", "255.255.255.0");
  Ipv4InterfaceContainer iAB = ipv4.Assign (dAB);
  ipv4.SetBase ("10.1.2.0", "255.255.255.0");
  Ipv4InterfaceContainer iAC = ipv4.Assign (dAC);
  ipv4.SetBase ("10.1.3.0", "255.255.255.0");
  Ipv4InterfaceContainer iBC = ipv4.Assign (dBC);
  ipv4.SetBase ("10.1.4.0", "255.255.255.0");
  Ipv4InterfaceContainer iBD = ipv4.Assign (dBD);
  ipv4.SetBase ("10.1.5.0", "255.255.255.0");
  Ipv4InterfaceContainer iCE = ipv4.Assign (dCE);
  ipv4.SetBase ("10.1.6.0", "255.255.255.0");
  Ipv4InterfaceContainer iEF = ipv4.Assign (dEF);
  ipv4.SetBase ("10.1.7.0", "255.255.255.0");
  Ipv4InterfaceContainer iEH = ipv4.Assign (dEH);
  ipv4.SetBase ("10.1.8.0", "255.255.255.0");
  Ipv4InterfaceContainer iDF = ipv4.Assign (dDF);
  ipv4.SetBase ("10.1.9.0", "255.255.255.0");
  Ipv4InterfaceContainer iFI = ipv4.Assign (dFI);
  ipv4.SetBase ("10.1.10.0", "255.255.255.0");
  Ipv4InterfaceContainer iGH = ipv4.Assign (dGH);
  ipv4.SetBase ("10.1.11.0", "255.255.255.0");
  Ipv4InterfaceContainer iHI = ipv4.Assign (dHI);
  ipv4.SetBase ("10.1.12.0", "255.255.255.0");
  Ipv4InterfaceContainer iGK = ipv4.Assign (dGK);
  ipv4.SetBase ("10.1.13.0", "255.255.255.0");
  Ipv4InterfaceContainer iIJ = ipv4.Assign (dIJ);
  ipv4.SetBase ("10.1.14.0", "255.255.255.0");
  Ipv4InterfaceContainer iJK = ipv4.Assign (dJK);

  std::vector <ns3::Ipv4Address> sinkAddresses;

  ipv4.SetBase ("192.168.1.0", "255.255.255.0");
  Ipv4InterfaceContainer iAAe = ipv4.Assign (dAAe);
  sinkAddresses.push_back(iAAe.GetAddress(1));
  ipv4.SetBase ("192.168.2.0", "255.255.255.0");
  Ipv4InterfaceContainer iBBe = ipv4.Assign (dBBe);
  sinkAddresses.push_back(iBBe.GetAddress(1));
  ipv4.SetBase ("192.168.3.0", "255.255.255.0");
  Ipv4InterfaceContainer iCCe = ipv4.Assign (dCCe);
  sinkAddresses.push_back(iCCe.GetAddress(1));
  ipv4.SetBase ("192.168.4.0", "255.255.255.0");
  Ipv4InterfaceContainer iDDe = ipv4.Assign (dDDe);
  sinkAddresses.push_back(iDDe.GetAddress(1));
  ipv4.SetBase ("192.168.5.0", "255.255.255.0");
  Ipv4InterfaceContainer iEEe = ipv4.Assign (dEEe);
  sinkAddresses.push_back(iEEe.GetAddress(1));
  ipv4.SetBase ("192.168.6.0", "255.255.255.0");
  Ipv4InterfaceContainer iFFe = ipv4.Assign (dFFe);
  sinkAddresses.push_back(iFFe.GetAddress(1));
  ipv4.SetBase ("192.168.7.0", "255.255.255.0");
  Ipv4InterfaceContainer iGGe = ipv4.Assign (dGGe);
  sinkAddresses.push_back(iGGe.GetAddress(1));
  ipv4.SetBase ("192.168.8.0", "255.255.255.0");
  Ipv4InterfaceContainer iHHe = ipv4.Assign (dHHe);
  sinkAddresses.push_back(iHHe.GetAddress(1));
  ipv4.SetBase ("192.168.9.0", "255.255.255.0");
  Ipv4InterfaceContainer iIIe = ipv4.Assign (dIIe);
  sinkAddresses.push_back(iIIe.GetAddress(1));
  ipv4.SetBase ("192.168.10.0", "255.255.255.0");
  Ipv4InterfaceContainer iJJe = ipv4.Assign (dJJe);
  sinkAddresses.push_back(iJJe.GetAddress(1));
  ipv4.SetBase ("192.168.11.0", "255.255.255.0");
  Ipv4InterfaceContainer iKKe = ipv4.Assign (dKKe);
  sinkAddresses.push_back(iKKe.GetAddress(1));


  Ptr<Ipv4> ipv4A = c.Get (0)->GetObject<Ipv4> ();
  Ptr<Ipv4> ipv4B = c.Get (1)->GetObject<Ipv4> ();
  Ptr<Ipv4> ipv4C = c.Get (2)->GetObject<Ipv4> ();
  Ptr<Ipv4> ipv4D = c.Get (3)->GetObject<Ipv4> ();
  Ptr<Ipv4> ipv4E = c.Get (4)->GetObject<Ipv4> ();
  Ptr<Ipv4> ipv4F = c.Get (5)->GetObject<Ipv4> ();
  Ptr<Ipv4> ipv4G = c.Get (6)->GetObject<Ipv4> ();
  Ptr<Ipv4> ipv4H = c.Get (7)->GetObject<Ipv4> ();
  Ptr<Ipv4> ipv4I = c.Get (8)->GetObject<Ipv4> ();
  Ptr<Ipv4> ipv4J = c.Get (9)->GetObject<Ipv4> ();
  Ptr<Ipv4> ipv4K = c.Get (10)->GetObject<Ipv4> ();

  Ptr<Ipv4> ipv4Ae = c_e.Get (0)->GetObject<Ipv4> ();
  Ptr<Ipv4> ipv4Be = c_e.Get (1)->GetObject<Ipv4> ();
  Ptr<Ipv4> ipv4Ce = c_e.Get (2)->GetObject<Ipv4> ();
  Ptr<Ipv4> ipv4De = c_e.Get (3)->GetObject<Ipv4> ();
  Ptr<Ipv4> ipv4Ee = c_e.Get (4)->GetObject<Ipv4> ();
  Ptr<Ipv4> ipv4Fe = c_e.Get (5)->GetObject<Ipv4> ();
  Ptr<Ipv4> ipv4Ge = c_e.Get (6)->GetObject<Ipv4> ();
  Ptr<Ipv4> ipv4He = c_e.Get (7)->GetObject<Ipv4> ();
  Ptr<Ipv4> ipv4Ie = c_e.Get (8)->GetObject<Ipv4> ();
  Ptr<Ipv4> ipv4Je = c_e.Get (9)->GetObject<Ipv4> ();
  Ptr<Ipv4> ipv4Ke = c_e.Get (10)->GetObject<Ipv4> ();

  Ipv4StaticRoutingHelper ipv4RoutingHelper;
  Ptr<Ipv4StaticRouting> staticRoutingA = ipv4RoutingHelper.GetStaticRouting (ipv4A);
  Ptr<Ipv4StaticRouting> staticRoutingB = ipv4RoutingHelper.GetStaticRouting (ipv4B);
  Ptr<Ipv4StaticRouting> staticRoutingC = ipv4RoutingHelper.GetStaticRouting (ipv4C);
  Ptr<Ipv4StaticRouting> staticRoutingD = ipv4RoutingHelper.GetStaticRouting (ipv4D);
  Ptr<Ipv4StaticRouting> staticRoutingE = ipv4RoutingHelper.GetStaticRouting (ipv4E);
  Ptr<Ipv4StaticRouting> staticRoutingF = ipv4RoutingHelper.GetStaticRouting (ipv4F);
  Ptr<Ipv4StaticRouting> staticRoutingG = ipv4RoutingHelper.GetStaticRouting (ipv4G);
  Ptr<Ipv4StaticRouting> staticRoutingH = ipv4RoutingHelper.GetStaticRouting (ipv4H);
  Ptr<Ipv4StaticRouting> staticRoutingI = ipv4RoutingHelper.GetStaticRouting (ipv4I);
  Ptr<Ipv4StaticRouting> staticRoutingJ = ipv4RoutingHelper.GetStaticRouting (ipv4J);
  Ptr<Ipv4StaticRouting> staticRoutingK = ipv4RoutingHelper.GetStaticRouting (ipv4K);

  Ptr<Ipv4StaticRouting> staticRoutingAe = ipv4RoutingHelper.GetStaticRouting (ipv4Ae);
  Ptr<Ipv4StaticRouting> staticRoutingBe = ipv4RoutingHelper.GetStaticRouting (ipv4Be);
  Ptr<Ipv4StaticRouting> staticRoutingCe = ipv4RoutingHelper.GetStaticRouting (ipv4Ce);
  Ptr<Ipv4StaticRouting> staticRoutingDe = ipv4RoutingHelper.GetStaticRouting (ipv4De);
  Ptr<Ipv4StaticRouting> staticRoutingEe = ipv4RoutingHelper.GetStaticRouting (ipv4Ee);
  Ptr<Ipv4StaticRouting> staticRoutingFe = ipv4RoutingHelper.GetStaticRouting (ipv4Fe);
  Ptr<Ipv4StaticRouting> staticRoutingGe = ipv4RoutingHelper.GetStaticRouting (ipv4Ge);
  Ptr<Ipv4StaticRouting> staticRoutingHe = ipv4RoutingHelper.GetStaticRouting (ipv4He);
  Ptr<Ipv4StaticRouting> staticRoutingIe = ipv4RoutingHelper.GetStaticRouting (ipv4Ie);
  Ptr<Ipv4StaticRouting> staticRoutingJe = ipv4RoutingHelper.GetStaticRouting (ipv4Je);
  Ptr<Ipv4StaticRouting> staticRoutingKe = ipv4RoutingHelper.GetStaticRouting (ipv4Ke);

  Ipv4Address fromLocal = Ipv4Address ("102.102.102.102");

  staticRoutingAe->AddHostRouteTo (iBBe.GetAddress (1), fromLocal, rvector ({1},{1}));
  staticRoutingAe->AddHostRouteTo (iCCe.GetAddress (1), fromLocal, rvector ({1},{1}));
  staticRoutingAe->AddHostRouteTo (iDDe.GetAddress (1), fromLocal, rvector ({1},{1}));
  staticRoutingAe->AddHostRouteTo (iEEe.GetAddress (1), fromLocal, rvector ({1},{1}));
  staticRoutingAe->AddHostRouteTo (iFFe.GetAddress (1), fromLocal, rvector ({1},{1}));
  staticRoutingAe->AddHostRouteTo (iGGe.GetAddress (1), fromLocal, rvector ({1},{1}));
  staticRoutingAe->AddHostRouteTo (iHHe.GetAddress (1), fromLocal, rvector ({1},{1}));
  staticRoutingAe->AddHostRouteTo (iIIe.GetAddress (1), fromLocal, rvector ({1},{1}));
  staticRoutingAe->AddHostRouteTo (iJJe.GetAddress (1), fromLocal, rvector ({1},{1}));
  staticRoutingAe->AddHostRouteTo (iKKe.GetAddress (1), fromLocal, rvector ({1},{1}));
  staticRoutingAe->AddHostRouteTo (iBBe.GetAddress (1), iAAe.GetAddress (1), rvector ({1},{1}));
  staticRoutingAe->AddHostRouteTo (iCCe.GetAddress (1), iAAe.GetAddress (1), rvector ({1},{1}));
  staticRoutingAe->AddHostRouteTo (iDDe.GetAddress (1), iAAe.GetAddress (1), rvector ({1},{1}));
  staticRoutingAe->AddHostRouteTo (iEEe.GetAddress (1), iAAe.GetAddress (1), rvector ({1},{1}));
  staticRoutingAe->AddHostRouteTo (iFFe.GetAddress (1), iAAe.GetAddress (1), rvector ({1},{1}));
  staticRoutingAe->AddHostRouteTo (iGGe.GetAddress (1), iAAe.GetAddress (1), rvector ({1},{1}));
  staticRoutingAe->AddHostRouteTo (iHHe.GetAddress (1), iAAe.GetAddress (1), rvector ({1},{1}));
  staticRoutingAe->AddHostRouteTo (iIIe.GetAddress (1), iAAe.GetAddress (1), rvector ({1},{1}));
  staticRoutingAe->AddHostRouteTo (iJJe.GetAddress (1), iAAe.GetAddress (1), rvector ({1},{1}));
  staticRoutingAe->AddHostRouteTo (iKKe.GetAddress (1), iAAe.GetAddress (1), rvector ({1},{1}));

  staticRoutingBe->AddHostRouteTo (iAAe.GetAddress (1), fromLocal, rvector ({1},{1}));
  staticRoutingBe->AddHostRouteTo (iCCe.GetAddress (1), fromLocal, rvector ({1},{1}));
  staticRoutingBe->AddHostRouteTo (iDDe.GetAddress (1), fromLocal, rvector ({1},{1}));
  staticRoutingBe->AddHostRouteTo (iEEe.GetAddress (1), fromLocal, rvector ({1},{1}));
  staticRoutingBe->AddHostRouteTo (iFFe.GetAddress (1), fromLocal, rvector ({1},{1}));
  staticRoutingBe->AddHostRouteTo (iGGe.GetAddress (1), fromLocal, rvector ({1},{1}));
  staticRoutingBe->AddHostRouteTo (iHHe.GetAddress (1), fromLocal, rvector ({1},{1}));
  staticRoutingBe->AddHostRouteTo (iIIe.GetAddress (1), fromLocal, rvector ({1},{1}));
  staticRoutingBe->AddHostRouteTo (iJJe.GetAddress (1), fromLocal, rvector ({1},{1}));
  staticRoutingBe->AddHostRouteTo (iKKe.GetAddress (1), fromLocal, rvector ({1},{1}));
  staticRoutingBe->AddHostRouteTo (iAAe.GetAddress (1), iBBe.GetAddress (1), rvector ({1},{1}));
  staticRoutingBe->AddHostRouteTo (iCCe.GetAddress (1), iBBe.GetAddress (1), rvector ({1},{1}));
  staticRoutingBe->AddHostRouteTo (iDDe.GetAddress (1), iBBe.GetAddress (1), rvector ({1},{1}));
  staticRoutingBe->AddHostRouteTo (iEEe.GetAddress (1), iBBe.GetAddress (1), rvector ({1},{1}));
  staticRoutingBe->AddHostRouteTo (iFFe.GetAddress (1), iBBe.GetAddress (1), rvector ({1},{1}));
  staticRoutingBe->AddHostRouteTo (iGGe.GetAddress (1), iBBe.GetAddress (1), rvector ({1},{1}));
  staticRoutingBe->AddHostRouteTo (iHHe.GetAddress (1), iBBe.GetAddress (1), rvector ({1},{1}));
  staticRoutingBe->AddHostRouteTo (iIIe.GetAddress (1), iBBe.GetAddress (1), rvector ({1},{1}));
  staticRoutingBe->AddHostRouteTo (iJJe.GetAddress (1), iBBe.GetAddress (1), rvector ({1},{1}));
  staticRoutingBe->AddHostRouteTo (iKKe.GetAddress (1), iBBe.GetAddress (1), rvector ({1},{1}));

  staticRoutingCe->AddHostRouteTo (iBBe.GetAddress (1), fromLocal, rvector ({1},{1}));
  staticRoutingCe->AddHostRouteTo (iAAe.GetAddress (1), fromLocal, rvector ({1},{1}));
  staticRoutingCe->AddHostRouteTo (iDDe.GetAddress (1), fromLocal, rvector ({1},{1}));
  staticRoutingCe->AddHostRouteTo (iEEe.GetAddress (1), fromLocal, rvector ({1},{1}));
  staticRoutingCe->AddHostRouteTo (iFFe.GetAddress (1), fromLocal, rvector ({1},{1}));
  staticRoutingCe->AddHostRouteTo (iGGe.GetAddress (1), fromLocal, rvector ({1},{1}));
  staticRoutingCe->AddHostRouteTo (iHHe.GetAddress (1), fromLocal, rvector ({1},{1}));
  staticRoutingCe->AddHostRouteTo (iIIe.GetAddress (1), fromLocal, rvector ({1},{1}));
  staticRoutingCe->AddHostRouteTo (iJJe.GetAddress (1), fromLocal, rvector ({1},{1}));
  staticRoutingCe->AddHostRouteTo (iKKe.GetAddress (1), fromLocal, rvector ({1},{1}));
  staticRoutingCe->AddHostRouteTo (iBBe.GetAddress (1), iCCe.GetAddress (1), rvector ({1},{1}));
  staticRoutingCe->AddHostRouteTo (iAAe.GetAddress (1), iCCe.GetAddress (1), rvector ({1},{1}));
  staticRoutingCe->AddHostRouteTo (iDDe.GetAddress (1), iCCe.GetAddress (1), rvector ({1},{1}));
  staticRoutingCe->AddHostRouteTo (iEEe.GetAddress (1), iCCe.GetAddress (1), rvector ({1},{1}));
  staticRoutingCe->AddHostRouteTo (iFFe.GetAddress (1), iCCe.GetAddress (1), rvector ({1},{1}));
  staticRoutingCe->AddHostRouteTo (iGGe.GetAddress (1), iCCe.GetAddress (1), rvector ({1},{1}));
  staticRoutingCe->AddHostRouteTo (iHHe.GetAddress (1), iCCe.GetAddress (1), rvector ({1},{1}));
  staticRoutingCe->AddHostRouteTo (iIIe.GetAddress (1), iCCe.GetAddress (1), rvector ({1},{1}));
  staticRoutingCe->AddHostRouteTo (iJJe.GetAddress (1), iCCe.GetAddress (1), rvector ({1},{1}));
  staticRoutingCe->AddHostRouteTo (iKKe.GetAddress (1), iCCe.GetAddress (1), rvector ({1},{1}));

  staticRoutingDe->AddHostRouteTo (iBBe.GetAddress (1), fromLocal, rvector ({1},{1}));
  staticRoutingDe->AddHostRouteTo (iCCe.GetAddress (1), fromLocal, rvector ({1},{1}));
  staticRoutingDe->AddHostRouteTo (iAAe.GetAddress (1), fromLocal, rvector ({1},{1}));
  staticRoutingDe->AddHostRouteTo (iEEe.GetAddress (1), fromLocal, rvector ({1},{1}));
  staticRoutingDe->AddHostRouteTo (iFFe.GetAddress (1), fromLocal, rvector ({1},{1}));
  staticRoutingDe->AddHostRouteTo (iGGe.GetAddress (1), fromLocal, rvector ({1},{1}));
  staticRoutingDe->AddHostRouteTo (iHHe.GetAddress (1), fromLocal, rvector ({1},{1}));
  staticRoutingDe->AddHostRouteTo (iIIe.GetAddress (1), fromLocal, rvector ({1},{1}));
  staticRoutingDe->AddHostRouteTo (iJJe.GetAddress (1), fromLocal, rvector ({1},{1}));
  staticRoutingDe->AddHostRouteTo (iKKe.GetAddress (1), fromLocal, rvector ({1},{1}));
  staticRoutingDe->AddHostRouteTo (iBBe.GetAddress (1), iDDe.GetAddress (1), rvector ({1},{1}));
  staticRoutingDe->AddHostRouteTo (iCCe.GetAddress (1), iDDe.GetAddress (1), rvector ({1},{1}));
  staticRoutingDe->AddHostRouteTo (iAAe.GetAddress (1), iDDe.GetAddress (1), rvector ({1},{1}));
  staticRoutingDe->AddHostRouteTo (iEEe.GetAddress (1), iDDe.GetAddress (1), rvector ({1},{1}));
  staticRoutingDe->AddHostRouteTo (iFFe.GetAddress (1), iDDe.GetAddress (1), rvector ({1},{1}));
  staticRoutingDe->AddHostRouteTo (iGGe.GetAddress (1), iDDe.GetAddress (1), rvector ({1},{1}));
  staticRoutingDe->AddHostRouteTo (iHHe.GetAddress (1), iDDe.GetAddress (1), rvector ({1},{1}));
  staticRoutingDe->AddHostRouteTo (iIIe.GetAddress (1), iDDe.GetAddress (1), rvector ({1},{1}));
  staticRoutingDe->AddHostRouteTo (iJJe.GetAddress (1), iDDe.GetAddress (1), rvector ({1},{1}));
  staticRoutingDe->AddHostRouteTo (iKKe.GetAddress (1), iDDe.GetAddress (1), rvector ({1},{1}));

  staticRoutingEe->AddHostRouteTo (iBBe.GetAddress (1), fromLocal, rvector ({1},{1}));
  staticRoutingEe->AddHostRouteTo (iCCe.GetAddress (1), fromLocal, rvector ({1},{1}));
  staticRoutingEe->AddHostRouteTo (iDDe.GetAddress (1), fromLocal, rvector ({1},{1}));
  staticRoutingEe->AddHostRouteTo (iAAe.GetAddress (1), fromLocal, rvector ({1},{1}));
  staticRoutingEe->AddHostRouteTo (iFFe.GetAddress (1), fromLocal, rvector ({1},{1}));
  staticRoutingEe->AddHostRouteTo (iGGe.GetAddress (1), fromLocal, rvector ({1},{1}));
  staticRoutingEe->AddHostRouteTo (iHHe.GetAddress (1), fromLocal, rvector ({1},{1}));
  staticRoutingEe->AddHostRouteTo (iIIe.GetAddress (1), fromLocal, rvector ({1},{1}));
  staticRoutingEe->AddHostRouteTo (iJJe.GetAddress (1), fromLocal, rvector ({1},{1}));
  staticRoutingEe->AddHostRouteTo (iKKe.GetAddress (1), fromLocal, rvector ({1},{1}));
  staticRoutingEe->AddHostRouteTo (iBBe.GetAddress (1), iEEe.GetAddress (1), rvector ({1},{1}));
  staticRoutingEe->AddHostRouteTo (iCCe.GetAddress (1), iEEe.GetAddress (1), rvector ({1},{1}));
  staticRoutingEe->AddHostRouteTo (iDDe.GetAddress (1), iEEe.GetAddress (1), rvector ({1},{1}));
  staticRoutingEe->AddHostRouteTo (iAAe.GetAddress (1), iEEe.GetAddress (1), rvector ({1},{1}));
  staticRoutingEe->AddHostRouteTo (iFFe.GetAddress (1), iEEe.GetAddress (1), rvector ({1},{1}));
  staticRoutingEe->AddHostRouteTo (iGGe.GetAddress (1), iEEe.GetAddress (1), rvector ({1},{1}));
  staticRoutingEe->AddHostRouteTo (iHHe.GetAddress (1), iEEe.GetAddress (1), rvector ({1},{1}));
  staticRoutingEe->AddHostRouteTo (iIIe.GetAddress (1), iEEe.GetAddress (1), rvector ({1},{1}));
  staticRoutingEe->AddHostRouteTo (iJJe.GetAddress (1), iEEe.GetAddress (1), rvector ({1},{1}));
  staticRoutingEe->AddHostRouteTo (iKKe.GetAddress (1), iEEe.GetAddress (1), rvector ({1},{1}));

  staticRoutingFe->AddHostRouteTo (iBBe.GetAddress (1), fromLocal, rvector ({1},{1}));
  staticRoutingFe->AddHostRouteTo (iCCe.GetAddress (1), fromLocal, rvector ({1},{1}));
  staticRoutingFe->AddHostRouteTo (iDDe.GetAddress (1), fromLocal, rvector ({1},{1}));
  staticRoutingFe->AddHostRouteTo (iEEe.GetAddress (1), fromLocal, rvector ({1},{1}));
  staticRoutingFe->AddHostRouteTo (iAAe.GetAddress (1), fromLocal, rvector ({1},{1}));
  staticRoutingFe->AddHostRouteTo (iGGe.GetAddress (1), fromLocal, rvector ({1},{1}));
  staticRoutingFe->AddHostRouteTo (iHHe.GetAddress (1), fromLocal, rvector ({1},{1}));
  staticRoutingFe->AddHostRouteTo (iIIe.GetAddress (1), fromLocal, rvector ({1},{1}));
  staticRoutingFe->AddHostRouteTo (iJJe.GetAddress (1), fromLocal, rvector ({1},{1}));
  staticRoutingFe->AddHostRouteTo (iKKe.GetAddress (1), fromLocal, rvector ({1},{1}));
  staticRoutingFe->AddHostRouteTo (iBBe.GetAddress (1), iFFe.GetAddress (1), rvector ({1},{1}));
  staticRoutingFe->AddHostRouteTo (iCCe.GetAddress (1), iFFe.GetAddress (1), rvector ({1},{1}));
  staticRoutingFe->AddHostRouteTo (iDDe.GetAddress (1), iFFe.GetAddress (1), rvector ({1},{1}));
  staticRoutingFe->AddHostRouteTo (iEEe.GetAddress (1), iFFe.GetAddress (1), rvector ({1},{1}));
  staticRoutingFe->AddHostRouteTo (iAAe.GetAddress (1), iFFe.GetAddress (1), rvector ({1},{1}));
  staticRoutingFe->AddHostRouteTo (iGGe.GetAddress (1), iFFe.GetAddress (1), rvector ({1},{1}));
  staticRoutingFe->AddHostRouteTo (iHHe.GetAddress (1), iFFe.GetAddress (1), rvector ({1},{1}));
  staticRoutingFe->AddHostRouteTo (iIIe.GetAddress (1), iFFe.GetAddress (1), rvector ({1},{1}));
  staticRoutingFe->AddHostRouteTo (iJJe.GetAddress (1), iFFe.GetAddress (1), rvector ({1},{1}));
  staticRoutingFe->AddHostRouteTo (iKKe.GetAddress (1), iFFe.GetAddress (1), rvector ({1},{1}));

  staticRoutingGe->AddHostRouteTo (iBBe.GetAddress (1), fromLocal, rvector ({1},{1}));
  staticRoutingGe->AddHostRouteTo (iCCe.GetAddress (1), fromLocal, rvector ({1},{1}));
  staticRoutingGe->AddHostRouteTo (iDDe.GetAddress (1), fromLocal, rvector ({1},{1}));
  staticRoutingGe->AddHostRouteTo (iEEe.GetAddress (1), fromLocal, rvector ({1},{1}));
  staticRoutingGe->AddHostRouteTo (iFFe.GetAddress (1), fromLocal, rvector ({1},{1}));
  staticRoutingGe->AddHostRouteTo (iAAe.GetAddress (1), fromLocal, rvector ({1},{1}));
  staticRoutingGe->AddHostRouteTo (iHHe.GetAddress (1), fromLocal, rvector ({1},{1}));
  staticRoutingGe->AddHostRouteTo (iIIe.GetAddress (1), fromLocal, rvector ({1},{1}));
  staticRoutingGe->AddHostRouteTo (iJJe.GetAddress (1), fromLocal, rvector ({1},{1}));
  staticRoutingGe->AddHostRouteTo (iKKe.GetAddress (1), fromLocal, rvector ({1},{1}));
  staticRoutingGe->AddHostRouteTo (iBBe.GetAddress (1), iGGe.GetAddress (1), rvector ({1},{1}));
  staticRoutingGe->AddHostRouteTo (iCCe.GetAddress (1), iGGe.GetAddress (1), rvector ({1},{1}));
  staticRoutingGe->AddHostRouteTo (iDDe.GetAddress (1), iGGe.GetAddress (1), rvector ({1},{1}));
  staticRoutingGe->AddHostRouteTo (iEEe.GetAddress (1), iGGe.GetAddress (1), rvector ({1},{1}));
  staticRoutingGe->AddHostRouteTo (iFFe.GetAddress (1), iGGe.GetAddress (1), rvector ({1},{1}));
  staticRoutingGe->AddHostRouteTo (iAAe.GetAddress (1), iGGe.GetAddress (1), rvector ({1},{1}));
  staticRoutingGe->AddHostRouteTo (iHHe.GetAddress (1), iGGe.GetAddress (1), rvector ({1},{1}));
  staticRoutingGe->AddHostRouteTo (iIIe.GetAddress (1), iGGe.GetAddress (1), rvector ({1},{1}));
  staticRoutingGe->AddHostRouteTo (iJJe.GetAddress (1), iGGe.GetAddress (1), rvector ({1},{1}));
  staticRoutingGe->AddHostRouteTo (iKKe.GetAddress (1), iGGe.GetAddress (1), rvector ({1},{1}));

  staticRoutingHe->AddHostRouteTo (iBBe.GetAddress (1), fromLocal, rvector ({1},{1}));
  staticRoutingHe->AddHostRouteTo (iCCe.GetAddress (1), fromLocal, rvector ({1},{1}));
  staticRoutingHe->AddHostRouteTo (iDDe.GetAddress (1), fromLocal, rvector ({1},{1}));
  staticRoutingHe->AddHostRouteTo (iEEe.GetAddress (1), fromLocal, rvector ({1},{1}));
  staticRoutingHe->AddHostRouteTo (iFFe.GetAddress (1), fromLocal, rvector ({1},{1}));
  staticRoutingHe->AddHostRouteTo (iGGe.GetAddress (1), fromLocal, rvector ({1},{1}));
  staticRoutingHe->AddHostRouteTo (iAAe.GetAddress (1), fromLocal, rvector ({1},{1}));
  staticRoutingHe->AddHostRouteTo (iIIe.GetAddress (1), fromLocal, rvector ({1},{1}));
  staticRoutingHe->AddHostRouteTo (iJJe.GetAddress (1), fromLocal, rvector ({1},{1}));
  staticRoutingHe->AddHostRouteTo (iKKe.GetAddress (1), fromLocal, rvector ({1},{1}));
  staticRoutingHe->AddHostRouteTo (iBBe.GetAddress (1), iHHe.GetAddress (1), rvector ({1},{1}));
  staticRoutingHe->AddHostRouteTo (iCCe.GetAddress (1), iHHe.GetAddress (1), rvector ({1},{1}));
  staticRoutingHe->AddHostRouteTo (iDDe.GetAddress (1), iHHe.GetAddress (1), rvector ({1},{1}));
  staticRoutingHe->AddHostRouteTo (iEEe.GetAddress (1), iHHe.GetAddress (1), rvector ({1},{1}));
  staticRoutingHe->AddHostRouteTo (iFFe.GetAddress (1), iHHe.GetAddress (1), rvector ({1},{1}));
  staticRoutingHe->AddHostRouteTo (iGGe.GetAddress (1), iHHe.GetAddress (1), rvector ({1},{1}));
  staticRoutingHe->AddHostRouteTo (iAAe.GetAddress (1), iHHe.GetAddress (1), rvector ({1},{1}));
  staticRoutingHe->AddHostRouteTo (iIIe.GetAddress (1), iHHe.GetAddress (1), rvector ({1},{1}));
  staticRoutingHe->AddHostRouteTo (iJJe.GetAddress (1), iHHe.GetAddress (1), rvector ({1},{1}));
  staticRoutingHe->AddHostRouteTo (iKKe.GetAddress (1), iHHe.GetAddress (1), rvector ({1},{1}));

  staticRoutingIe->AddHostRouteTo (iBBe.GetAddress (1), fromLocal, rvector ({1},{1}));
  staticRoutingIe->AddHostRouteTo (iCCe.GetAddress (1), fromLocal, rvector ({1},{1}));
  staticRoutingIe->AddHostRouteTo (iDDe.GetAddress (1), fromLocal, rvector ({1},{1}));
  staticRoutingIe->AddHostRouteTo (iEEe.GetAddress (1), fromLocal, rvector ({1},{1}));
  staticRoutingIe->AddHostRouteTo (iFFe.GetAddress (1), fromLocal, rvector ({1},{1}));
  staticRoutingIe->AddHostRouteTo (iGGe.GetAddress (1), fromLocal, rvector ({1},{1}));
  staticRoutingIe->AddHostRouteTo (iHHe.GetAddress (1), fromLocal, rvector ({1},{1}));
  staticRoutingIe->AddHostRouteTo (iAAe.GetAddress (1), fromLocal, rvector ({1},{1}));
  staticRoutingIe->AddHostRouteTo (iJJe.GetAddress (1), fromLocal, rvector ({1},{1}));
  staticRoutingIe->AddHostRouteTo (iKKe.GetAddress (1), fromLocal, rvector ({1},{1}));
  staticRoutingIe->AddHostRouteTo (iBBe.GetAddress (1), iIIe.GetAddress (1), rvector ({1},{1}));
  staticRoutingIe->AddHostRouteTo (iCCe.GetAddress (1), iIIe.GetAddress (1), rvector ({1},{1}));
  staticRoutingIe->AddHostRouteTo (iDDe.GetAddress (1), iIIe.GetAddress (1), rvector ({1},{1}));
  staticRoutingIe->AddHostRouteTo (iEEe.GetAddress (1), iIIe.GetAddress (1), rvector ({1},{1}));
  staticRoutingIe->AddHostRouteTo (iFFe.GetAddress (1), iIIe.GetAddress (1), rvector ({1},{1}));
  staticRoutingIe->AddHostRouteTo (iGGe.GetAddress (1), iIIe.GetAddress (1), rvector ({1},{1}));
  staticRoutingIe->AddHostRouteTo (iHHe.GetAddress (1), iIIe.GetAddress (1), rvector ({1},{1}));
  staticRoutingIe->AddHostRouteTo (iAAe.GetAddress (1), iIIe.GetAddress (1), rvector ({1},{1}));
  staticRoutingIe->AddHostRouteTo (iJJe.GetAddress (1), iIIe.GetAddress (1), rvector ({1},{1}));
  staticRoutingIe->AddHostRouteTo (iKKe.GetAddress (1), iIIe.GetAddress (1), rvector ({1},{1}));

  staticRoutingJe->AddHostRouteTo (iBBe.GetAddress (1), fromLocal, rvector ({1},{1}));
  staticRoutingJe->AddHostRouteTo (iCCe.GetAddress (1), fromLocal, rvector ({1},{1}));
  staticRoutingJe->AddHostRouteTo (iDDe.GetAddress (1), fromLocal, rvector ({1},{1}));
  staticRoutingJe->AddHostRouteTo (iEEe.GetAddress (1), fromLocal, rvector ({1},{1}));
  staticRoutingJe->AddHostRouteTo (iFFe.GetAddress (1), fromLocal, rvector ({1},{1}));
  staticRoutingJe->AddHostRouteTo (iGGe.GetAddress (1), fromLocal, rvector ({1},{1}));
  staticRoutingJe->AddHostRouteTo (iHHe.GetAddress (1), fromLocal, rvector ({1},{1}));
  staticRoutingJe->AddHostRouteTo (iIIe.GetAddress (1), fromLocal, rvector ({1},{1}));
  staticRoutingJe->AddHostRouteTo (iAAe.GetAddress (1), fromLocal, rvector ({1},{1}));
  staticRoutingJe->AddHostRouteTo (iKKe.GetAddress (1), fromLocal, rvector ({1},{1}));
  staticRoutingJe->AddHostRouteTo (iBBe.GetAddress (1), iJJe.GetAddress (1), rvector ({1},{1}));
  staticRoutingJe->AddHostRouteTo (iCCe.GetAddress (1), iJJe.GetAddress (1), rvector ({1},{1}));
  staticRoutingJe->AddHostRouteTo (iDDe.GetAddress (1), iJJe.GetAddress (1), rvector ({1},{1}));
  staticRoutingJe->AddHostRouteTo (iEEe.GetAddress (1), iJJe.GetAddress (1), rvector ({1},{1}));
  staticRoutingJe->AddHostRouteTo (iFFe.GetAddress (1), iJJe.GetAddress (1), rvector ({1},{1}));
  staticRoutingJe->AddHostRouteTo (iGGe.GetAddress (1), iJJe.GetAddress (1), rvector ({1},{1}));
  staticRoutingJe->AddHostRouteTo (iHHe.GetAddress (1), iJJe.GetAddress (1), rvector ({1},{1}));
  staticRoutingJe->AddHostRouteTo (iIIe.GetAddress (1), iJJe.GetAddress (1), rvector ({1},{1}));
  staticRoutingJe->AddHostRouteTo (iAAe.GetAddress (1), iJJe.GetAddress (1), rvector ({1},{1}));
  staticRoutingJe->AddHostRouteTo (iKKe.GetAddress (1), iJJe.GetAddress (1), rvector ({1},{1}));

  staticRoutingKe->AddHostRouteTo (iBBe.GetAddress (1), fromLocal, rvector ({1},{1}));
  staticRoutingKe->AddHostRouteTo (iCCe.GetAddress (1), fromLocal, rvector ({1},{1}));
  staticRoutingKe->AddHostRouteTo (iDDe.GetAddress (1), fromLocal, rvector ({1},{1}));
  staticRoutingKe->AddHostRouteTo (iEEe.GetAddress (1), fromLocal, rvector ({1},{1}));
  staticRoutingKe->AddHostRouteTo (iFFe.GetAddress (1), fromLocal, rvector ({1},{1}));
  staticRoutingKe->AddHostRouteTo (iGGe.GetAddress (1), fromLocal, rvector ({1},{1}));
  staticRoutingKe->AddHostRouteTo (iHHe.GetAddress (1), fromLocal, rvector ({1},{1}));
  staticRoutingKe->AddHostRouteTo (iIIe.GetAddress (1), fromLocal, rvector ({1},{1}));
  staticRoutingKe->AddHostRouteTo (iJJe.GetAddress (1), fromLocal, rvector ({1},{1}));
  staticRoutingKe->AddHostRouteTo (iAAe.GetAddress (1), fromLocal, rvector ({1},{1}));
  staticRoutingKe->AddHostRouteTo (iBBe.GetAddress (1), iKKe.GetAddress (1), rvector ({1},{1}));
  staticRoutingKe->AddHostRouteTo (iCCe.GetAddress (1), iKKe.GetAddress (1), rvector ({1},{1}));
  staticRoutingKe->AddHostRouteTo (iDDe.GetAddress (1), iKKe.GetAddress (1), rvector ({1},{1}));
  staticRoutingKe->AddHostRouteTo (iEEe.GetAddress (1), iKKe.GetAddress (1), rvector ({1},{1}));
  staticRoutingKe->AddHostRouteTo (iFFe.GetAddress (1), iKKe.GetAddress (1), rvector ({1},{1}));
  staticRoutingKe->AddHostRouteTo (iGGe.GetAddress (1), iKKe.GetAddress (1), rvector ({1},{1}));
  staticRoutingKe->AddHostRouteTo (iHHe.GetAddress (1), iKKe.GetAddress (1), rvector ({1},{1}));
  staticRoutingKe->AddHostRouteTo (iIIe.GetAddress (1), iKKe.GetAddress (1), rvector ({1},{1}));
  staticRoutingKe->AddHostRouteTo (iJJe.GetAddress (1), iKKe.GetAddress (1), rvector ({1},{1}));
  staticRoutingKe->AddHostRouteTo (iAAe.GetAddress (1), iKKe.GetAddress (1), rvector ({1},{1}));
  
  staticRoutingA->AddHostRouteTo (iBBe.GetAddress(1), iAAe.GetAddress(1), rvector({1},{1}));
  staticRoutingB->AddHostRouteTo (iBBe.GetAddress(1), iAAe.GetAddress(1), rvector({4},{1}));
  
  staticRoutingB->AddHostRouteTo (iAAe.GetAddress(1), iBBe.GetAddress(1), rvector({1},{1}));
  staticRoutingA->AddHostRouteTo (iAAe.GetAddress(1), iBBe.GetAddress(1), rvector({3},{1}));
  

/////////////////////////////////////

  // Setup sink App

    std::array<ApplicationContainer, 11*100> sinkApps;
    for(int i = 0; i <= 10; i++){
      for (int j = 1; j <= 100; j++)
      {
        PacketSinkHelper packetSinkHelper ("ns3::TcpSocketFactory", InetSocketAddress (Ipv4Address::GetAny (), j));
        sinkApps[i*100+j-1] = packetSinkHelper.Install (c_e.Get (i));
        sinkApps[i*100+j-1].Start (Seconds (0.));
        sinkApps[i*100+j-1].Stop (Seconds (END_TIME));
      }
    }
  // Setup sink App end

  // Setup source application
    TypeId tid = TypeId::LookupByName ("ns3::TcpSocketFactory");
    for (int i = 0; i <= 10; i++)
    {
      for (int j = 0; j <= 10; j++)
      {
        if (j != i)
        {
          for (int k = 1; k <= 100; k++)
          {
            Ptr<MyApp> app = CreateObject<MyApp> ();
            Ptr<Node> node = c_e.Get (i);
            Address sinkAddress = InetSocketAddress (sinkAddresses[j], k);
            if (i == 4 && j == 5)
            {
              app->Setup (tid, node ,sinkAddress, PACKET_SIZE, 300000, DataRate ("0.28Mbps"), "n" + std::to_string(i) + "-n" + std::to_string(j)+"p"+std::to_string(k));
            } else {
              app->Setup (tid, node ,sinkAddress, PACKET_SIZE, NUM_PACKETS, DataRate (DEFAULT_SEND_RATE), "n" + std::to_string(i) + "-n" + std::to_string(j)+"p"+std::to_string(k));
            }
            node->AddApplication (app);
            app->SetStartTime (Seconds (k/100));
            app->SetStopTime (Seconds (END_TIME));
          }
        }
      }
    }
  // Setup source application end

  // Trace settings
    AsciiTraceHelper ascii;
    streamLinkTrafSize = ascii.CreateFileStream ("./matrix/link.traf");
    streamLinkPktCount = ascii.CreateFileStream ("./matrix/link.pktc");
    streamLinkLossCount = ascii.CreateFileStream ("./matrix/link.loss");

    Simulator::Schedule(Time (Seconds (INTERVAL)), &monitorLink, INTERVAL);
    *streamLinkTrafSize->GetStream ()<< INTERVAL <<"\n\n";
    *streamLinkTrafSize->GetStream ()<< END_TIME / INTERVAL <<"\n\n";
  // Trace settings

  // Animation settings
    AnimationInterface::SetConstantPosition (c.Get (0),2.0,2.0);
    AnimationInterface::SetConstantPosition (c.Get (1),2.0,4.0);
    AnimationInterface::SetConstantPosition (c.Get (2),4.0,4.0);
    AnimationInterface::SetConstantPosition (c.Get (3),3.0,6.0);
    AnimationInterface::SetConstantPosition (c.Get (4),6.0,4.0);
    AnimationInterface::SetConstantPosition (c.Get (5),6.0,6.0);
    AnimationInterface::SetConstantPosition (c.Get (6),8.0,3.0);
    AnimationInterface::SetConstantPosition (c.Get (7),8.0,4.0);
    AnimationInterface::SetConstantPosition (c.Get (8),8.0,6.0);
    AnimationInterface::SetConstantPosition (c.Get (9),9.0,5.0);
    AnimationInterface::SetConstantPosition (c.Get (10),10.0,4.0);

    AnimationInterface::SetConstantPosition (c_e.Get (0),1.5,2.0);
    AnimationInterface::SetConstantPosition (c_e.Get (1),1.5,4.0);
    AnimationInterface::SetConstantPosition (c_e.Get (2),4.0,4.5);
    AnimationInterface::SetConstantPosition (c_e.Get (3),3.0,6.5);
    AnimationInterface::SetConstantPosition (c_e.Get (4),6.0,3.5);
    AnimationInterface::SetConstantPosition (c_e.Get (5),6.0,6.5);
    AnimationInterface::SetConstantPosition (c_e.Get (6),8.0,2.5);
    AnimationInterface::SetConstantPosition (c_e.Get (7),8.5,4.0);
    AnimationInterface::SetConstantPosition (c_e.Get (8),8.0,6.5);
    AnimationInterface::SetConstantPosition (c_e.Get (9),9.5,5.5);
    AnimationInterface::SetConstantPosition (c_e.Get (10),10.5,4.0);
    AnimationInterface anim ("./Data/static-route-default.xml");
  //Animation settings end

  Simulator::Stop (Seconds (END_TIME));
  Simulator::Run ();
  Simulator::Destroy ();
  return 0;
}