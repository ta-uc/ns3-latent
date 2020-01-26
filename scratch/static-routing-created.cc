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
#define DEFAULT_SEND_RATE "5Mbps"
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
  NodeContainer c;
  c.Create (11);

  InternetStackHelper internet;
  internet.Install (c);

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

  PointToPointHelper p2p;
  p2p.SetChannelAttribute ("Delay", StringValue ("1ms"));
  
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

  Ptr<CsmaNetDevice> deviceA = CreateObject<CsmaNetDevice> ();
  deviceA->SetAddress (Mac48Address::Allocate ());
  c.Get(0)->AddDevice (deviceA);
  deviceA->SetQueue (CreateObject<DropTailQueue<Packet> > ());
  Ptr<CsmaNetDevice> deviceB = CreateObject<CsmaNetDevice> ();
  deviceB->SetAddress (Mac48Address::Allocate ());
  c.Get(1)->AddDevice (deviceB);
  deviceB->SetQueue (CreateObject<DropTailQueue<Packet> > ());
  Ptr<CsmaNetDevice> deviceC = CreateObject<CsmaNetDevice> ();
  deviceC->SetAddress (Mac48Address::Allocate ());
  c.Get(2)->AddDevice (deviceC);
  deviceC->SetQueue (CreateObject<DropTailQueue<Packet> > ());
  Ptr<CsmaNetDevice> deviceD = CreateObject<CsmaNetDevice> ();
  deviceD->SetAddress (Mac48Address::Allocate ());
  c.Get(3)->AddDevice (deviceD);
  deviceD->SetQueue (CreateObject<DropTailQueue<Packet> > ());
  Ptr<CsmaNetDevice> deviceE = CreateObject<CsmaNetDevice> ();
  deviceE->SetAddress (Mac48Address::Allocate ());
  c.Get(4)->AddDevice (deviceE);
  deviceE->SetQueue (CreateObject<DropTailQueue<Packet> > ());
  Ptr<CsmaNetDevice> deviceF = CreateObject<CsmaNetDevice> ();
  deviceF->SetAddress (Mac48Address::Allocate ());
  c.Get(5)->AddDevice (deviceF);
  deviceF->SetQueue (CreateObject<DropTailQueue<Packet> > ());
  Ptr<CsmaNetDevice> deviceG = CreateObject<CsmaNetDevice> ();
  deviceG->SetAddress (Mac48Address::Allocate ());
  c.Get(6)->AddDevice (deviceG);
  deviceG->SetQueue (CreateObject<DropTailQueue<Packet> > ());
  Ptr<CsmaNetDevice> deviceH = CreateObject<CsmaNetDevice> ();
  deviceH->SetAddress (Mac48Address::Allocate ());
  c.Get(7)->AddDevice (deviceH);
  deviceH->SetQueue (CreateObject<DropTailQueue<Packet> > ());
  Ptr<CsmaNetDevice> deviceI = CreateObject<CsmaNetDevice> ();
  deviceI->SetAddress (Mac48Address::Allocate ());
  c.Get(8)->AddDevice (deviceI);
  deviceI->SetQueue (CreateObject<DropTailQueue<Packet> > ());
  Ptr<CsmaNetDevice> deviceJ = CreateObject<CsmaNetDevice> ();
  deviceJ->SetAddress (Mac48Address::Allocate ());
  c.Get(9)->AddDevice (deviceJ);
  deviceJ->SetQueue (CreateObject<DropTailQueue<Packet> > ());
  Ptr<CsmaNetDevice> deviceK = CreateObject<CsmaNetDevice> ();
  deviceK->SetAddress (Mac48Address::Allocate ());
  c.Get(10)->AddDevice (deviceK);
  deviceK->SetQueue (CreateObject<DropTailQueue<Packet> > ());

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

  int32_t ifIndexA = ipv4A->AddInterface (deviceA);
  int32_t ifIndexB = ipv4B->AddInterface (deviceB);
  int32_t ifIndexC = ipv4C->AddInterface (deviceC);
  int32_t ifIndexD = ipv4D->AddInterface (deviceD);
  int32_t ifIndexE = ipv4E->AddInterface (deviceE);
  int32_t ifIndexF = ipv4F->AddInterface (deviceF);
  int32_t ifIndexG = ipv4G->AddInterface (deviceG);
  int32_t ifIndexH = ipv4H->AddInterface (deviceH);
  int32_t ifIndexI = ipv4I->AddInterface (deviceI);
  int32_t ifIndexJ = ipv4J->AddInterface (deviceJ);
  int32_t ifIndexK = ipv4K->AddInterface (deviceK);

  std::vector <ns3::Ipv4Address> sinkAddresses;

  Ipv4InterfaceAddress ifInAddrA = Ipv4InterfaceAddress (Ipv4Address ("172.16.1.1"), Ipv4Mask ("/32"));
  ipv4A->AddAddress (ifIndexA, ifInAddrA);
  ipv4A->SetMetric (ifIndexA, 1);
  ipv4A->SetUp (ifIndexA);
  sinkAddresses.push_back(ifInAddrA.GetLocal ());
  Ipv4InterfaceAddress ifInAddrB = Ipv4InterfaceAddress (Ipv4Address ("172.16.1.2"), Ipv4Mask ("/32"));
  ipv4B->AddAddress (ifIndexB, ifInAddrB);
  ipv4B->SetMetric (ifIndexB, 1);
  ipv4B->SetUp (ifIndexB);
  sinkAddresses.push_back(ifInAddrB.GetLocal ());
  Ipv4InterfaceAddress ifInAddrC = Ipv4InterfaceAddress (Ipv4Address ("172.16.1.3"), Ipv4Mask ("/32"));
  ipv4C->AddAddress (ifIndexC, ifInAddrC);
  ipv4C->SetMetric (ifIndexC, 1);
  ipv4C->SetUp (ifIndexC);
  sinkAddresses.push_back(ifInAddrC.GetLocal ());
  Ipv4InterfaceAddress ifInAddrD = Ipv4InterfaceAddress (Ipv4Address ("172.16.1.4"), Ipv4Mask ("/32"));
  ipv4D->AddAddress (ifIndexD, ifInAddrD);
  ipv4D->SetMetric (ifIndexD, 1);
  ipv4D->SetUp (ifIndexD);
  sinkAddresses.push_back(ifInAddrD.GetLocal ());
  Ipv4InterfaceAddress ifInAddrE = Ipv4InterfaceAddress (Ipv4Address ("172.16.1.5"), Ipv4Mask ("/32"));
  ipv4E->AddAddress (ifIndexE, ifInAddrE);
  ipv4E->SetMetric (ifIndexE, 1);
  ipv4E->SetUp (ifIndexE);
  sinkAddresses.push_back(ifInAddrE.GetLocal ());
  Ipv4InterfaceAddress ifInAddrF = Ipv4InterfaceAddress (Ipv4Address ("172.16.1.6"), Ipv4Mask ("/32"));
  ipv4F->AddAddress (ifIndexF, ifInAddrF);
  ipv4F->SetMetric (ifIndexF, 1);
  ipv4F->SetUp (ifIndexF);
  sinkAddresses.push_back(ifInAddrF.GetLocal ());
  Ipv4InterfaceAddress ifInAddrG = Ipv4InterfaceAddress (Ipv4Address ("172.16.1.7"), Ipv4Mask ("/32"));
  ipv4G->AddAddress (ifIndexG, ifInAddrG);
  ipv4G->SetMetric (ifIndexG, 1);
  ipv4G->SetUp (ifIndexG);
  sinkAddresses.push_back(ifInAddrG.GetLocal ());
  Ipv4InterfaceAddress ifInAddrH = Ipv4InterfaceAddress (Ipv4Address ("172.16.1.8"), Ipv4Mask ("/32"));
  ipv4H->AddAddress (ifIndexH, ifInAddrH);
  ipv4H->SetMetric (ifIndexH, 1);
  ipv4H->SetUp (ifIndexH);
  sinkAddresses.push_back(ifInAddrH.GetLocal ());
  Ipv4InterfaceAddress ifInAddrI = Ipv4InterfaceAddress (Ipv4Address ("172.16.1.9"), Ipv4Mask ("/32"));
  ipv4I->AddAddress (ifIndexI, ifInAddrI);
  ipv4I->SetMetric (ifIndexI, 1);
  ipv4I->SetUp (ifIndexI);
  sinkAddresses.push_back(ifInAddrI.GetLocal ());
  Ipv4InterfaceAddress ifInAddrJ = Ipv4InterfaceAddress (Ipv4Address ("172.16.1.10"), Ipv4Mask ("/32"));
  ipv4J->AddAddress (ifIndexJ, ifInAddrJ);
  ipv4J->SetMetric (ifIndexJ, 1);
  ipv4J->SetUp (ifIndexJ);
  sinkAddresses.push_back(ifInAddrJ.GetLocal ());
  Ipv4InterfaceAddress ifInAddrK = Ipv4InterfaceAddress (Ipv4Address ("172.16.1.11"), Ipv4Mask ("/32"));
  ipv4K->AddAddress (ifIndexK, ifInAddrK);
  ipv4K->SetMetric (ifIndexK, 1);
  ipv4K->SetUp (ifIndexK);
  sinkAddresses.push_back(ifInAddrK.GetLocal ());

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

  Ipv4Address fromLocal = Ipv4Address ("102.102.102.102");
  staticRoutingA->AddHostRouteTo (Ipv4Address ("172.16.1.2"), fromLocal, rvector ({1},{1}));//A->B(normal packet)
  staticRoutingA->AddHostRouteTo (Ipv4Address ("172.16.1.2"), iAB.GetAddress (0,0), rvector ({1},{1}));//A->B(TCP)
  staticRoutingA->AddHostRouteTo (Ipv4Address ("172.16.1.2"), ifInAddrA.GetLocal (), rvector ({1},{1}));//A->B(TCP)
  staticRoutingA->AddHostRouteTo (iAB.GetAddress (1,0), ifInAddrA.GetLocal (), rvector ({1},{1}));//A->B(TCP)
  staticRoutingA->AddHostRouteTo (iBC.GetAddress (0,0), ifInAddrA.GetLocal (), rvector ({1},{1}));//A->B(TCP)
  staticRoutingA->AddHostRouteTo (iBD.GetAddress (0,0), ifInAddrA.GetLocal (), rvector ({1},{1}));//A->B(TCP)

  staticRoutingA->AddHostRouteTo (Ipv4Address ("172.16.1.3"), fromLocal, rvector ({2},{1}));//A->C(normal packet)
  staticRoutingA->AddHostRouteTo (Ipv4Address ("172.16.1.3"), iAC.GetAddress (0,0), rvector ({2},{1}));//A->C(TCP)
  staticRoutingA->AddHostRouteTo (Ipv4Address ("172.16.1.3"), ifInAddrA.GetLocal (), rvector ({2},{1}));//A->C(TCP)
  staticRoutingA->AddHostRouteTo (iAC.GetAddress (1,0), ifInAddrA.GetLocal (), rvector ({2},{1}));//A->C(TCP)
  staticRoutingA->AddHostRouteTo (iBC.GetAddress (1,0), ifInAddrA.GetLocal (), rvector ({2},{1}));//A->C(TCP)
  staticRoutingA->AddHostRouteTo (iCE.GetAddress (0,0), ifInAddrA.GetLocal (), rvector ({2},{1}));//A->C(TCP)

  staticRoutingA->AddHostRouteTo (Ipv4Address ("172.16.1.4"), fromLocal, rvector ({1},{1}));//A->B(normal packet)
  staticRoutingA->AddHostRouteTo (Ipv4Address ("172.16.1.4"), iAB.GetAddress (0,0), rvector ({1},{1}));//A->B(TCP)
  staticRoutingA->AddHostRouteTo (Ipv4Address ("172.16.1.4"), ifInAddrA.GetLocal (), rvector ({1},{1}));//A->B(TCP)
  staticRoutingA->AddHostRouteTo (iBD.GetAddress (1,0), ifInAddrA.GetLocal (), rvector ({1},{1}));//A->B(TCP)
  staticRoutingA->AddHostRouteTo (iDF.GetAddress (0,0), ifInAddrA.GetLocal (), rvector ({1},{1}));//A->B(TCP)
  staticRoutingB->AddHostRouteTo (Ipv4Address ("172.16.1.4"), iAB.GetAddress (0,0), rvector ({3},{1}));//B->D
  staticRoutingB->AddHostRouteTo (Ipv4Address ("172.16.1.4"), ifInAddrA.GetLocal (), rvector ({3},{1}));//B->D(TCP)
  staticRoutingB->AddHostRouteTo (iBD.GetAddress (1,0), ifInAddrA.GetLocal (), rvector ({3},{1}));//B->D(TCP)
  staticRoutingB->AddHostRouteTo (iDF.GetAddress (0,0), ifInAddrA.GetLocal (), rvector ({3},{1}));//B->D(TCP)

  staticRoutingA->AddHostRouteTo (Ipv4Address ("172.16.1.5"), fromLocal, rvector ({2},{1}));//A->C(normal packet)
  staticRoutingA->AddHostRouteTo (Ipv4Address ("172.16.1.5"), iAC.GetAddress (0,0), rvector ({2},{1}));//A->C(TCP)
  staticRoutingA->AddHostRouteTo (Ipv4Address ("172.16.1.5"), ifInAddrA.GetLocal (), rvector ({2},{1}));//A->C(TCP)
  staticRoutingA->AddHostRouteTo (iCE.GetAddress (1,0), ifInAddrA.GetLocal (), rvector ({2},{1}));//A->C(TCP)
  staticRoutingA->AddHostRouteTo (iEF.GetAddress (0,0), ifInAddrA.GetLocal (), rvector ({2},{1}));//A->C(TCP)
  staticRoutingA->AddHostRouteTo (iEH.GetAddress (0,0), ifInAddrA.GetLocal (), rvector ({2},{1}));//A->C(TCP)
  staticRoutingC->AddHostRouteTo (Ipv4Address ("172.16.1.5"), iAC.GetAddress (0,0), rvector ({3},{1}));//C->E
  staticRoutingC->AddHostRouteTo (Ipv4Address ("172.16.1.5"), ifInAddrA.GetLocal (), rvector ({3},{1}));//C->E(TCP)
  staticRoutingC->AddHostRouteTo (iCE.GetAddress (1,0), ifInAddrA.GetLocal (), rvector ({3},{1}));//C->E(TCP)
  staticRoutingC->AddHostRouteTo (iEF.GetAddress (0,0), ifInAddrA.GetLocal (), rvector ({3},{1}));//C->E(TCP)
  staticRoutingC->AddHostRouteTo (iEH.GetAddress (0,0), ifInAddrA.GetLocal (), rvector ({3},{1}));//C->E(TCP)

  staticRoutingA->AddHostRouteTo (Ipv4Address ("172.16.1.6"), fromLocal, rvector ({1},{1}));//A->B(normal packet)
  staticRoutingA->AddHostRouteTo (Ipv4Address ("172.16.1.6"), iAB.GetAddress (0,0), rvector ({1},{1}));//A->B(TCP)
  staticRoutingA->AddHostRouteTo (Ipv4Address ("172.16.1.6"), ifInAddrA.GetLocal (), rvector ({1},{1}));//A->B(TCP)
  staticRoutingA->AddHostRouteTo (iEF.GetAddress (1,0), ifInAddrA.GetLocal (), rvector ({1},{1}));//A->B(TCP)
  staticRoutingA->AddHostRouteTo (iDF.GetAddress (1,0), ifInAddrA.GetLocal (), rvector ({1},{1}));//A->B(TCP)
  staticRoutingA->AddHostRouteTo (iFI.GetAddress (0,0), ifInAddrA.GetLocal (), rvector ({1},{1}));//A->B(TCP)
  staticRoutingB->AddHostRouteTo (Ipv4Address ("172.16.1.6"), iAB.GetAddress (0,0), rvector ({3},{1}));//B->D
  staticRoutingB->AddHostRouteTo (Ipv4Address ("172.16.1.6"), ifInAddrA.GetLocal (), rvector ({3},{1}));//B->D(TCP)
  staticRoutingB->AddHostRouteTo (iEF.GetAddress (1,0), ifInAddrA.GetLocal (), rvector ({3},{1}));//B->D(TCP)
  staticRoutingB->AddHostRouteTo (iDF.GetAddress (1,0), ifInAddrA.GetLocal (), rvector ({3},{1}));//B->D(TCP)
  staticRoutingB->AddHostRouteTo (iFI.GetAddress (0,0), ifInAddrA.GetLocal (), rvector ({3},{1}));//B->D(TCP)
  staticRoutingD->AddHostRouteTo (Ipv4Address ("172.16.1.6"), iAB.GetAddress (0,0), rvector ({2},{1}));//D->F
  staticRoutingD->AddHostRouteTo (Ipv4Address ("172.16.1.6"), ifInAddrA.GetLocal (), rvector ({2},{1}));//D->F(TCP)
  staticRoutingD->AddHostRouteTo (iEF.GetAddress (1,0), ifInAddrA.GetLocal (), rvector ({2},{1}));//D->F(TCP)
  staticRoutingD->AddHostRouteTo (iDF.GetAddress (1,0), ifInAddrA.GetLocal (), rvector ({2},{1}));//D->F(TCP)
  staticRoutingD->AddHostRouteTo (iFI.GetAddress (0,0), ifInAddrA.GetLocal (), rvector ({2},{1}));//D->F(TCP)

  staticRoutingA->AddHostRouteTo (Ipv4Address ("172.16.1.7"), fromLocal, rvector ({2},{1}));//A->C(normal packet)
  staticRoutingA->AddHostRouteTo (Ipv4Address ("172.16.1.7"), iAC.GetAddress (0,0), rvector ({2},{1}));//A->C(TCP)
  staticRoutingA->AddHostRouteTo (Ipv4Address ("172.16.1.7"), ifInAddrA.GetLocal (), rvector ({2},{1}));//A->C(TCP)
  staticRoutingA->AddHostRouteTo (iGH.GetAddress (0,0), ifInAddrA.GetLocal (), rvector ({2},{1}));//A->C(TCP)
  staticRoutingA->AddHostRouteTo (iGK.GetAddress (0,0), ifInAddrA.GetLocal (), rvector ({2},{1}));//A->C(TCP)
  staticRoutingC->AddHostRouteTo (Ipv4Address ("172.16.1.7"), iAC.GetAddress (0,0), rvector ({3},{1}));//C->E
  staticRoutingC->AddHostRouteTo (Ipv4Address ("172.16.1.7"), ifInAddrA.GetLocal (), rvector ({3},{1}));//C->E(TCP)
  staticRoutingC->AddHostRouteTo (iGH.GetAddress (0,0), ifInAddrA.GetLocal (), rvector ({3},{1}));//C->E(TCP)
  staticRoutingC->AddHostRouteTo (iGK.GetAddress (0,0), ifInAddrA.GetLocal (), rvector ({3},{1}));//C->E(TCP)
  staticRoutingE->AddHostRouteTo (Ipv4Address ("172.16.1.7"), iAC.GetAddress (0,0), rvector ({3},{1}));//E->H
  staticRoutingE->AddHostRouteTo (Ipv4Address ("172.16.1.7"), ifInAddrA.GetLocal (), rvector ({3},{1}));//E->H(TCP)
  staticRoutingE->AddHostRouteTo (iGH.GetAddress (0,0), ifInAddrA.GetLocal (), rvector ({3},{1}));//E->H(TCP)
  staticRoutingE->AddHostRouteTo (iGK.GetAddress (0,0), ifInAddrA.GetLocal (), rvector ({3},{1}));//E->H(TCP)
  staticRoutingH->AddHostRouteTo (Ipv4Address ("172.16.1.7"), iAC.GetAddress (0,0), rvector ({2},{1}));//H->G
  staticRoutingH->AddHostRouteTo (Ipv4Address ("172.16.1.7"), ifInAddrA.GetLocal (), rvector ({2},{1}));//H->G(TCP)
  staticRoutingH->AddHostRouteTo (iGH.GetAddress (0,0), ifInAddrA.GetLocal (), rvector ({2},{1}));//H->G(TCP)
  staticRoutingH->AddHostRouteTo (iGK.GetAddress (0,0), ifInAddrA.GetLocal (), rvector ({2},{1}));//H->G(TCP)

  staticRoutingA->AddHostRouteTo (Ipv4Address ("172.16.1.8"), fromLocal, rvector ({2},{1}));//A->C(normal packet)
  staticRoutingA->AddHostRouteTo (Ipv4Address ("172.16.1.8"), iAC.GetAddress (0,0), rvector ({2},{1}));//A->C(TCP)
  staticRoutingA->AddHostRouteTo (Ipv4Address ("172.16.1.8"), ifInAddrA.GetLocal (), rvector ({2},{1}));//A->C(TCP)
  staticRoutingA->AddHostRouteTo (iEH.GetAddress (1,0), ifInAddrA.GetLocal (), rvector ({2},{1}));//A->C(TCP)
  staticRoutingA->AddHostRouteTo (iGH.GetAddress (1,0), ifInAddrA.GetLocal (), rvector ({2},{1}));//A->C(TCP)
  staticRoutingA->AddHostRouteTo (iHI.GetAddress (0,0), ifInAddrA.GetLocal (), rvector ({2},{1}));//A->C(TCP)
  staticRoutingC->AddHostRouteTo (Ipv4Address ("172.16.1.8"), iAC.GetAddress (0,0), rvector ({3},{1}));//C->E
  staticRoutingC->AddHostRouteTo (Ipv4Address ("172.16.1.8"), ifInAddrA.GetLocal (), rvector ({3},{1}));//C->E(TCP)
  staticRoutingC->AddHostRouteTo (iEH.GetAddress (1,0), ifInAddrA.GetLocal (), rvector ({3},{1}));//C->E(TCP)
  staticRoutingC->AddHostRouteTo (iGH.GetAddress (1,0), ifInAddrA.GetLocal (), rvector ({3},{1}));//C->E(TCP)
  staticRoutingC->AddHostRouteTo (iHI.GetAddress (0,0), ifInAddrA.GetLocal (), rvector ({3},{1}));//C->E(TCP)
  staticRoutingE->AddHostRouteTo (Ipv4Address ("172.16.1.8"), iAC.GetAddress (0,0), rvector ({3},{1}));//E->H
  staticRoutingE->AddHostRouteTo (Ipv4Address ("172.16.1.8"), ifInAddrA.GetLocal (), rvector ({3},{1}));//E->H(TCP)
  staticRoutingE->AddHostRouteTo (iEH.GetAddress (1,0), ifInAddrA.GetLocal (), rvector ({3},{1}));//E->H(TCP)
  staticRoutingE->AddHostRouteTo (iGH.GetAddress (1,0), ifInAddrA.GetLocal (), rvector ({3},{1}));//E->H(TCP)
  staticRoutingE->AddHostRouteTo (iHI.GetAddress (0,0), ifInAddrA.GetLocal (), rvector ({3},{1}));//E->H(TCP)

  staticRoutingA->AddHostRouteTo (Ipv4Address ("172.16.1.9"), fromLocal, rvector ({1},{1}));//A->B(normal packet)
  staticRoutingA->AddHostRouteTo (Ipv4Address ("172.16.1.9"), iAB.GetAddress (0,0), rvector ({1},{1}));//A->B(TCP)
  staticRoutingA->AddHostRouteTo (Ipv4Address ("172.16.1.9"), ifInAddrA.GetLocal (), rvector ({1},{1}));//A->B(TCP)
  staticRoutingA->AddHostRouteTo (iFI.GetAddress (1,0), ifInAddrA.GetLocal (), rvector ({1},{1}));//A->B(TCP)
  staticRoutingA->AddHostRouteTo (iHI.GetAddress (1,0), ifInAddrA.GetLocal (), rvector ({1},{1}));//A->B(TCP)
  staticRoutingA->AddHostRouteTo (iIJ.GetAddress (0,0), ifInAddrA.GetLocal (), rvector ({1},{1}));//A->B(TCP)
  staticRoutingB->AddHostRouteTo (Ipv4Address ("172.16.1.9"), iAB.GetAddress (0,0), rvector ({3},{1}));//B->D
  staticRoutingB->AddHostRouteTo (Ipv4Address ("172.16.1.9"), ifInAddrA.GetLocal (), rvector ({3},{1}));//B->D(TCP)
  staticRoutingB->AddHostRouteTo (iFI.GetAddress (1,0), ifInAddrA.GetLocal (), rvector ({3},{1}));//B->D(TCP)
  staticRoutingB->AddHostRouteTo (iHI.GetAddress (1,0), ifInAddrA.GetLocal (), rvector ({3},{1}));//B->D(TCP)
  staticRoutingB->AddHostRouteTo (iIJ.GetAddress (0,0), ifInAddrA.GetLocal (), rvector ({3},{1}));//B->D(TCP)
  staticRoutingD->AddHostRouteTo (Ipv4Address ("172.16.1.9"), iAB.GetAddress (0,0), rvector ({2},{1}));//D->F
  staticRoutingD->AddHostRouteTo (Ipv4Address ("172.16.1.9"), ifInAddrA.GetLocal (), rvector ({2},{1}));//D->F(TCP)
  staticRoutingD->AddHostRouteTo (iFI.GetAddress (1,0), ifInAddrA.GetLocal (), rvector ({2},{1}));//D->F(TCP)
  staticRoutingD->AddHostRouteTo (iHI.GetAddress (1,0), ifInAddrA.GetLocal (), rvector ({2},{1}));//D->F(TCP)
  staticRoutingD->AddHostRouteTo (iIJ.GetAddress (0,0), ifInAddrA.GetLocal (), rvector ({2},{1}));//D->F(TCP)
  staticRoutingF->AddHostRouteTo (Ipv4Address ("172.16.1.9"), iAB.GetAddress (0,0), rvector ({3},{1}));//F->I
  staticRoutingF->AddHostRouteTo (Ipv4Address ("172.16.1.9"), ifInAddrA.GetLocal (), rvector ({3},{1}));//F->I(TCP)
  staticRoutingF->AddHostRouteTo (iFI.GetAddress (1,0), ifInAddrA.GetLocal (), rvector ({3},{1}));//F->I(TCP)
  staticRoutingF->AddHostRouteTo (iHI.GetAddress (1,0), ifInAddrA.GetLocal (), rvector ({3},{1}));//F->I(TCP)
  staticRoutingF->AddHostRouteTo (iIJ.GetAddress (0,0), ifInAddrA.GetLocal (), rvector ({3},{1}));//F->I(TCP)

  staticRoutingA->AddHostRouteTo (Ipv4Address ("172.16.1.10"), fromLocal, rvector ({1},{1}));//A->B(normal packet)
  staticRoutingA->AddHostRouteTo (Ipv4Address ("172.16.1.10"), iAB.GetAddress (0,0), rvector ({1},{1}));//A->B(TCP)
  staticRoutingA->AddHostRouteTo (Ipv4Address ("172.16.1.10"), ifInAddrA.GetLocal (), rvector ({1},{1}));//A->B(TCP)
  staticRoutingA->AddHostRouteTo (iIJ.GetAddress (1,0), ifInAddrA.GetLocal (), rvector ({1},{1}));//A->B(TCP)
  staticRoutingA->AddHostRouteTo (iJK.GetAddress (0,0), ifInAddrA.GetLocal (), rvector ({1},{1}));//A->B(TCP)
  staticRoutingB->AddHostRouteTo (Ipv4Address ("172.16.1.10"), iAB.GetAddress (0,0), rvector ({3},{1}));//B->D
  staticRoutingB->AddHostRouteTo (Ipv4Address ("172.16.1.10"), ifInAddrA.GetLocal (), rvector ({3},{1}));//B->D(TCP)
  staticRoutingB->AddHostRouteTo (iIJ.GetAddress (1,0), ifInAddrA.GetLocal (), rvector ({3},{1}));//B->D(TCP)
  staticRoutingB->AddHostRouteTo (iJK.GetAddress (0,0), ifInAddrA.GetLocal (), rvector ({3},{1}));//B->D(TCP)
  staticRoutingD->AddHostRouteTo (Ipv4Address ("172.16.1.10"), iAB.GetAddress (0,0), rvector ({2},{1}));//D->F
  staticRoutingD->AddHostRouteTo (Ipv4Address ("172.16.1.10"), ifInAddrA.GetLocal (), rvector ({2},{1}));//D->F(TCP)
  staticRoutingD->AddHostRouteTo (iIJ.GetAddress (1,0), ifInAddrA.GetLocal (), rvector ({2},{1}));//D->F(TCP)
  staticRoutingD->AddHostRouteTo (iJK.GetAddress (0,0), ifInAddrA.GetLocal (), rvector ({2},{1}));//D->F(TCP)
  staticRoutingF->AddHostRouteTo (Ipv4Address ("172.16.1.10"), iAB.GetAddress (0,0), rvector ({3},{1}));//F->I
  staticRoutingF->AddHostRouteTo (Ipv4Address ("172.16.1.10"), ifInAddrA.GetLocal (), rvector ({3},{1}));//F->I(TCP)
  staticRoutingF->AddHostRouteTo (iIJ.GetAddress (1,0), ifInAddrA.GetLocal (), rvector ({3},{1}));//F->I(TCP)
  staticRoutingF->AddHostRouteTo (iJK.GetAddress (0,0), ifInAddrA.GetLocal (), rvector ({3},{1}));//F->I(TCP)
  staticRoutingI->AddHostRouteTo (Ipv4Address ("172.16.1.10"), iAB.GetAddress (0,0), rvector ({3},{1}));//I->J
  staticRoutingI->AddHostRouteTo (Ipv4Address ("172.16.1.10"), ifInAddrA.GetLocal (), rvector ({3},{1}));//I->J(TCP)
  staticRoutingI->AddHostRouteTo (iIJ.GetAddress (1,0), ifInAddrA.GetLocal (), rvector ({3},{1}));//I->J(TCP)
  staticRoutingI->AddHostRouteTo (iJK.GetAddress (0,0), ifInAddrA.GetLocal (), rvector ({3},{1}));//I->J(TCP)

  staticRoutingA->AddHostRouteTo (Ipv4Address ("172.16.1.11"), fromLocal, rvector ({2},{1}));//A->C(normal packet)
  staticRoutingA->AddHostRouteTo (Ipv4Address ("172.16.1.11"), iAC.GetAddress (0,0), rvector ({2},{1}));//A->C(TCP)
  staticRoutingA->AddHostRouteTo (Ipv4Address ("172.16.1.11"), ifInAddrA.GetLocal (), rvector ({2},{1}));//A->C(TCP)
  staticRoutingA->AddHostRouteTo (iGK.GetAddress (1,0), ifInAddrA.GetLocal (), rvector ({2},{1}));//A->C(TCP)
  staticRoutingA->AddHostRouteTo (iJK.GetAddress (1,0), ifInAddrA.GetLocal (), rvector ({2},{1}));//A->C(TCP)
  staticRoutingC->AddHostRouteTo (Ipv4Address ("172.16.1.11"), iAC.GetAddress (0,0), rvector ({3},{1}));//C->E
  staticRoutingC->AddHostRouteTo (Ipv4Address ("172.16.1.11"), ifInAddrA.GetLocal (), rvector ({3},{1}));//C->E(TCP)
  staticRoutingC->AddHostRouteTo (iGK.GetAddress (1,0), ifInAddrA.GetLocal (), rvector ({3},{1}));//C->E(TCP)
  staticRoutingC->AddHostRouteTo (iJK.GetAddress (1,0), ifInAddrA.GetLocal (), rvector ({3},{1}));//C->E(TCP)
  staticRoutingE->AddHostRouteTo (Ipv4Address ("172.16.1.11"), iAC.GetAddress (0,0), rvector ({3},{1}));//E->H
  staticRoutingE->AddHostRouteTo (Ipv4Address ("172.16.1.11"), ifInAddrA.GetLocal (), rvector ({3},{1}));//E->H(TCP)
  staticRoutingE->AddHostRouteTo (iGK.GetAddress (1,0), ifInAddrA.GetLocal (), rvector ({3},{1}));//E->H(TCP)
  staticRoutingE->AddHostRouteTo (iJK.GetAddress (1,0), ifInAddrA.GetLocal (), rvector ({3},{1}));//E->H(TCP)
  staticRoutingH->AddHostRouteTo (Ipv4Address ("172.16.1.11"), iAC.GetAddress (0,0), rvector ({2},{1}));//H->G
  staticRoutingH->AddHostRouteTo (Ipv4Address ("172.16.1.11"), ifInAddrA.GetLocal (), rvector ({2},{1}));//H->G(TCP)
  staticRoutingH->AddHostRouteTo (iGK.GetAddress (1,0), ifInAddrA.GetLocal (), rvector ({2},{1}));//H->G(TCP)
  staticRoutingH->AddHostRouteTo (iJK.GetAddress (1,0), ifInAddrA.GetLocal (), rvector ({2},{1}));//H->G(TCP)
  staticRoutingG->AddHostRouteTo (Ipv4Address ("172.16.1.11"), iAC.GetAddress (0,0), rvector ({2},{1}));//G->K
  staticRoutingG->AddHostRouteTo (Ipv4Address ("172.16.1.11"), ifInAddrA.GetLocal (), rvector ({2},{1}));//G->K(TCP)
  staticRoutingG->AddHostRouteTo (iGK.GetAddress (1,0), ifInAddrA.GetLocal (), rvector ({2},{1}));//G->K(TCP)
  staticRoutingG->AddHostRouteTo (iJK.GetAddress (1,0), ifInAddrA.GetLocal (), rvector ({2},{1}));//G->K(TCP)

  staticRoutingB->AddHostRouteTo (Ipv4Address ("172.16.1.1"), fromLocal, rvector ({1},{1}));//B->A(normal packet)
  staticRoutingB->AddHostRouteTo (Ipv4Address ("172.16.1.1"), iAB.GetAddress (1,0), rvector ({1},{1}));//B->A(TCP)
  staticRoutingB->AddHostRouteTo (Ipv4Address ("172.16.1.1"), ifInAddrB.GetLocal (), rvector ({1},{1}));//B->A(TCP)
  staticRoutingB->AddHostRouteTo (iAB.GetAddress (0,0), ifInAddrB.GetLocal (), rvector ({1},{1}));//B->A(TCP)
  staticRoutingB->AddHostRouteTo (iAC.GetAddress (0,0), ifInAddrB.GetLocal (), rvector ({1},{1}));//B->A(TCP)

  staticRoutingB->AddHostRouteTo (Ipv4Address ("172.16.1.3"), fromLocal, rvector ({2},{1}));//B->C(normal packet)
  staticRoutingB->AddHostRouteTo (Ipv4Address ("172.16.1.3"), iBC.GetAddress (0,0), rvector ({2},{1}));//B->C(TCP)
  staticRoutingB->AddHostRouteTo (Ipv4Address ("172.16.1.3"), ifInAddrB.GetLocal (), rvector ({2},{1}));//B->C(TCP)
  staticRoutingB->AddHostRouteTo (iAC.GetAddress (1,0), ifInAddrB.GetLocal (), rvector ({2},{1}));//B->C(TCP)
  staticRoutingB->AddHostRouteTo (iBC.GetAddress (1,0), ifInAddrB.GetLocal (), rvector ({2},{1}));//B->C(TCP)
  staticRoutingB->AddHostRouteTo (iCE.GetAddress (0,0), ifInAddrB.GetLocal (), rvector ({2},{1}));//B->C(TCP)

  staticRoutingB->AddHostRouteTo (Ipv4Address ("172.16.1.4"), fromLocal, rvector ({3},{1}));//B->D(normal packet)
  staticRoutingB->AddHostRouteTo (Ipv4Address ("172.16.1.4"), iBD.GetAddress (0,0), rvector ({3},{1}));//B->D(TCP)
  staticRoutingB->AddHostRouteTo (Ipv4Address ("172.16.1.4"), ifInAddrB.GetLocal (), rvector ({3},{1}));//B->D(TCP)
  staticRoutingB->AddHostRouteTo (iBD.GetAddress (1,0), ifInAddrB.GetLocal (), rvector ({3},{1}));//B->D(TCP)
  staticRoutingB->AddHostRouteTo (iDF.GetAddress (0,0), ifInAddrB.GetLocal (), rvector ({3},{1}));//B->D(TCP)

  staticRoutingB->AddHostRouteTo (Ipv4Address ("172.16.1.5"), fromLocal, rvector ({2},{1}));//B->C(normal packet)
  staticRoutingB->AddHostRouteTo (Ipv4Address ("172.16.1.5"), iBC.GetAddress (0,0), rvector ({2},{1}));//B->C(TCP)
  staticRoutingB->AddHostRouteTo (Ipv4Address ("172.16.1.5"), ifInAddrB.GetLocal (), rvector ({2},{1}));//B->C(TCP)
  staticRoutingB->AddHostRouteTo (iCE.GetAddress (1,0), ifInAddrB.GetLocal (), rvector ({2},{1}));//B->C(TCP)
  staticRoutingB->AddHostRouteTo (iEF.GetAddress (0,0), ifInAddrB.GetLocal (), rvector ({2},{1}));//B->C(TCP)
  staticRoutingB->AddHostRouteTo (iEH.GetAddress (0,0), ifInAddrB.GetLocal (), rvector ({2},{1}));//B->C(TCP)
  staticRoutingC->AddHostRouteTo (Ipv4Address ("172.16.1.5"), iBC.GetAddress (0,0), rvector ({3},{1}));//C->E
  staticRoutingC->AddHostRouteTo (Ipv4Address ("172.16.1.5"), ifInAddrB.GetLocal (), rvector ({3},{1}));//C->E(TCP)
  staticRoutingC->AddHostRouteTo (iCE.GetAddress (1,0), ifInAddrB.GetLocal (), rvector ({3},{1}));//C->E(TCP)
  staticRoutingC->AddHostRouteTo (iEF.GetAddress (0,0), ifInAddrB.GetLocal (), rvector ({3},{1}));//C->E(TCP)
  staticRoutingC->AddHostRouteTo (iEH.GetAddress (0,0), ifInAddrB.GetLocal (), rvector ({3},{1}));//C->E(TCP)

  staticRoutingB->AddHostRouteTo (Ipv4Address ("172.16.1.6"), fromLocal, rvector ({3},{1}));//B->D(normal packet)
  staticRoutingB->AddHostRouteTo (Ipv4Address ("172.16.1.6"), iBD.GetAddress (0,0), rvector ({3},{1}));//B->D(TCP)
  staticRoutingB->AddHostRouteTo (Ipv4Address ("172.16.1.6"), ifInAddrB.GetLocal (), rvector ({3},{1}));//B->D(TCP)
  staticRoutingB->AddHostRouteTo (iEF.GetAddress (1,0), ifInAddrB.GetLocal (), rvector ({3},{1}));//B->D(TCP)
  staticRoutingB->AddHostRouteTo (iDF.GetAddress (1,0), ifInAddrB.GetLocal (), rvector ({3},{1}));//B->D(TCP)
  staticRoutingB->AddHostRouteTo (iFI.GetAddress (0,0), ifInAddrB.GetLocal (), rvector ({3},{1}));//B->D(TCP)
  staticRoutingD->AddHostRouteTo (Ipv4Address ("172.16.1.6"), iBD.GetAddress (0,0), rvector ({2},{1}));//D->F
  staticRoutingD->AddHostRouteTo (Ipv4Address ("172.16.1.6"), ifInAddrB.GetLocal (), rvector ({2},{1}));//D->F(TCP)
  staticRoutingD->AddHostRouteTo (iEF.GetAddress (1,0), ifInAddrB.GetLocal (), rvector ({2},{1}));//D->F(TCP)
  staticRoutingD->AddHostRouteTo (iDF.GetAddress (1,0), ifInAddrB.GetLocal (), rvector ({2},{1}));//D->F(TCP)
  staticRoutingD->AddHostRouteTo (iFI.GetAddress (0,0), ifInAddrB.GetLocal (), rvector ({2},{1}));//D->F(TCP)

  staticRoutingB->AddHostRouteTo (Ipv4Address ("172.16.1.7"), fromLocal, rvector ({2},{1}));//B->C(normal packet)
  staticRoutingB->AddHostRouteTo (Ipv4Address ("172.16.1.7"), iBC.GetAddress (0,0), rvector ({2},{1}));//B->C(TCP)
  staticRoutingB->AddHostRouteTo (Ipv4Address ("172.16.1.7"), ifInAddrB.GetLocal (), rvector ({2},{1}));//B->C(TCP)
  staticRoutingB->AddHostRouteTo (iGH.GetAddress (0,0), ifInAddrB.GetLocal (), rvector ({2},{1}));//B->C(TCP)
  staticRoutingB->AddHostRouteTo (iGK.GetAddress (0,0), ifInAddrB.GetLocal (), rvector ({2},{1}));//B->C(TCP)
  staticRoutingC->AddHostRouteTo (Ipv4Address ("172.16.1.7"), iBC.GetAddress (0,0), rvector ({3},{1}));//C->E
  staticRoutingC->AddHostRouteTo (Ipv4Address ("172.16.1.7"), ifInAddrB.GetLocal (), rvector ({3},{1}));//C->E(TCP)
  staticRoutingC->AddHostRouteTo (iGH.GetAddress (0,0), ifInAddrB.GetLocal (), rvector ({3},{1}));//C->E(TCP)
  staticRoutingC->AddHostRouteTo (iGK.GetAddress (0,0), ifInAddrB.GetLocal (), rvector ({3},{1}));//C->E(TCP)
  staticRoutingE->AddHostRouteTo (Ipv4Address ("172.16.1.7"), iBC.GetAddress (0,0), rvector ({3},{1}));//E->H
  staticRoutingE->AddHostRouteTo (Ipv4Address ("172.16.1.7"), ifInAddrB.GetLocal (), rvector ({3},{1}));//E->H(TCP)
  staticRoutingE->AddHostRouteTo (iGH.GetAddress (0,0), ifInAddrB.GetLocal (), rvector ({3},{1}));//E->H(TCP)
  staticRoutingE->AddHostRouteTo (iGK.GetAddress (0,0), ifInAddrB.GetLocal (), rvector ({3},{1}));//E->H(TCP)
  staticRoutingH->AddHostRouteTo (Ipv4Address ("172.16.1.7"), iBC.GetAddress (0,0), rvector ({2},{1}));//H->G
  staticRoutingH->AddHostRouteTo (Ipv4Address ("172.16.1.7"), ifInAddrB.GetLocal (), rvector ({2},{1}));//H->G(TCP)
  staticRoutingH->AddHostRouteTo (iGH.GetAddress (0,0), ifInAddrB.GetLocal (), rvector ({2},{1}));//H->G(TCP)
  staticRoutingH->AddHostRouteTo (iGK.GetAddress (0,0), ifInAddrB.GetLocal (), rvector ({2},{1}));//H->G(TCP)

  staticRoutingB->AddHostRouteTo (Ipv4Address ("172.16.1.8"), fromLocal, rvector ({2},{1}));//B->C(normal packet)
  staticRoutingB->AddHostRouteTo (Ipv4Address ("172.16.1.8"), iBC.GetAddress (0,0), rvector ({2},{1}));//B->C(TCP)
  staticRoutingB->AddHostRouteTo (Ipv4Address ("172.16.1.8"), ifInAddrB.GetLocal (), rvector ({2},{1}));//B->C(TCP)
  staticRoutingB->AddHostRouteTo (iEH.GetAddress (1,0), ifInAddrB.GetLocal (), rvector ({2},{1}));//B->C(TCP)
  staticRoutingB->AddHostRouteTo (iGH.GetAddress (1,0), ifInAddrB.GetLocal (), rvector ({2},{1}));//B->C(TCP)
  staticRoutingB->AddHostRouteTo (iHI.GetAddress (0,0), ifInAddrB.GetLocal (), rvector ({2},{1}));//B->C(TCP)
  staticRoutingC->AddHostRouteTo (Ipv4Address ("172.16.1.8"), iBC.GetAddress (0,0), rvector ({3},{1}));//C->E
  staticRoutingC->AddHostRouteTo (Ipv4Address ("172.16.1.8"), ifInAddrB.GetLocal (), rvector ({3},{1}));//C->E(TCP)
  staticRoutingC->AddHostRouteTo (iEH.GetAddress (1,0), ifInAddrB.GetLocal (), rvector ({3},{1}));//C->E(TCP)
  staticRoutingC->AddHostRouteTo (iGH.GetAddress (1,0), ifInAddrB.GetLocal (), rvector ({3},{1}));//C->E(TCP)
  staticRoutingC->AddHostRouteTo (iHI.GetAddress (0,0), ifInAddrB.GetLocal (), rvector ({3},{1}));//C->E(TCP)
  staticRoutingE->AddHostRouteTo (Ipv4Address ("172.16.1.8"), iBC.GetAddress (0,0), rvector ({3},{1}));//E->H
  staticRoutingE->AddHostRouteTo (Ipv4Address ("172.16.1.8"), ifInAddrB.GetLocal (), rvector ({3},{1}));//E->H(TCP)
  staticRoutingE->AddHostRouteTo (iEH.GetAddress (1,0), ifInAddrB.GetLocal (), rvector ({3},{1}));//E->H(TCP)
  staticRoutingE->AddHostRouteTo (iGH.GetAddress (1,0), ifInAddrB.GetLocal (), rvector ({3},{1}));//E->H(TCP)
  staticRoutingE->AddHostRouteTo (iHI.GetAddress (0,0), ifInAddrB.GetLocal (), rvector ({3},{1}));//E->H(TCP)

  staticRoutingB->AddHostRouteTo (Ipv4Address ("172.16.1.9"), fromLocal, rvector ({3},{1}));//B->D(normal packet)
  staticRoutingB->AddHostRouteTo (Ipv4Address ("172.16.1.9"), iBD.GetAddress (0,0), rvector ({3},{1}));//B->D(TCP)
  staticRoutingB->AddHostRouteTo (Ipv4Address ("172.16.1.9"), ifInAddrB.GetLocal (), rvector ({3},{1}));//B->D(TCP)
  staticRoutingB->AddHostRouteTo (iFI.GetAddress (1,0), ifInAddrB.GetLocal (), rvector ({3},{1}));//B->D(TCP)
  staticRoutingB->AddHostRouteTo (iHI.GetAddress (1,0), ifInAddrB.GetLocal (), rvector ({3},{1}));//B->D(TCP)
  staticRoutingB->AddHostRouteTo (iIJ.GetAddress (0,0), ifInAddrB.GetLocal (), rvector ({3},{1}));//B->D(TCP)
  staticRoutingD->AddHostRouteTo (Ipv4Address ("172.16.1.9"), iBD.GetAddress (0,0), rvector ({2},{1}));//D->F
  staticRoutingD->AddHostRouteTo (Ipv4Address ("172.16.1.9"), ifInAddrB.GetLocal (), rvector ({2},{1}));//D->F(TCP)
  staticRoutingD->AddHostRouteTo (iFI.GetAddress (1,0), ifInAddrB.GetLocal (), rvector ({2},{1}));//D->F(TCP)
  staticRoutingD->AddHostRouteTo (iHI.GetAddress (1,0), ifInAddrB.GetLocal (), rvector ({2},{1}));//D->F(TCP)
  staticRoutingD->AddHostRouteTo (iIJ.GetAddress (0,0), ifInAddrB.GetLocal (), rvector ({2},{1}));//D->F(TCP)
  staticRoutingF->AddHostRouteTo (Ipv4Address ("172.16.1.9"), iBD.GetAddress (0,0), rvector ({3},{1}));//F->I
  staticRoutingF->AddHostRouteTo (Ipv4Address ("172.16.1.9"), ifInAddrB.GetLocal (), rvector ({3},{1}));//F->I(TCP)
  staticRoutingF->AddHostRouteTo (iFI.GetAddress (1,0), ifInAddrB.GetLocal (), rvector ({3},{1}));//F->I(TCP)
  staticRoutingF->AddHostRouteTo (iHI.GetAddress (1,0), ifInAddrB.GetLocal (), rvector ({3},{1}));//F->I(TCP)
  staticRoutingF->AddHostRouteTo (iIJ.GetAddress (0,0), ifInAddrB.GetLocal (), rvector ({3},{1}));//F->I(TCP)

  staticRoutingB->AddHostRouteTo (Ipv4Address ("172.16.1.10"), fromLocal, rvector ({3},{1}));//B->D(normal packet)
  staticRoutingB->AddHostRouteTo (Ipv4Address ("172.16.1.10"), iBD.GetAddress (0,0), rvector ({3},{1}));//B->D(TCP)
  staticRoutingB->AddHostRouteTo (Ipv4Address ("172.16.1.10"), ifInAddrB.GetLocal (), rvector ({3},{1}));//B->D(TCP)
  staticRoutingB->AddHostRouteTo (iIJ.GetAddress (1,0), ifInAddrB.GetLocal (), rvector ({3},{1}));//B->D(TCP)
  staticRoutingB->AddHostRouteTo (iJK.GetAddress (0,0), ifInAddrB.GetLocal (), rvector ({3},{1}));//B->D(TCP)
  staticRoutingD->AddHostRouteTo (Ipv4Address ("172.16.1.10"), iBD.GetAddress (0,0), rvector ({2},{1}));//D->F
  staticRoutingD->AddHostRouteTo (Ipv4Address ("172.16.1.10"), ifInAddrB.GetLocal (), rvector ({2},{1}));//D->F(TCP)
  staticRoutingD->AddHostRouteTo (iIJ.GetAddress (1,0), ifInAddrB.GetLocal (), rvector ({2},{1}));//D->F(TCP)
  staticRoutingD->AddHostRouteTo (iJK.GetAddress (0,0), ifInAddrB.GetLocal (), rvector ({2},{1}));//D->F(TCP)
  staticRoutingF->AddHostRouteTo (Ipv4Address ("172.16.1.10"), iBD.GetAddress (0,0), rvector ({3},{1}));//F->I
  staticRoutingF->AddHostRouteTo (Ipv4Address ("172.16.1.10"), ifInAddrB.GetLocal (), rvector ({3},{1}));//F->I(TCP)
  staticRoutingF->AddHostRouteTo (iIJ.GetAddress (1,0), ifInAddrB.GetLocal (), rvector ({3},{1}));//F->I(TCP)
  staticRoutingF->AddHostRouteTo (iJK.GetAddress (0,0), ifInAddrB.GetLocal (), rvector ({3},{1}));//F->I(TCP)
  staticRoutingI->AddHostRouteTo (Ipv4Address ("172.16.1.10"), iBD.GetAddress (0,0), rvector ({3},{1}));//I->J
  staticRoutingI->AddHostRouteTo (Ipv4Address ("172.16.1.10"), ifInAddrB.GetLocal (), rvector ({3},{1}));//I->J(TCP)
  staticRoutingI->AddHostRouteTo (iIJ.GetAddress (1,0), ifInAddrB.GetLocal (), rvector ({3},{1}));//I->J(TCP)
  staticRoutingI->AddHostRouteTo (iJK.GetAddress (0,0), ifInAddrB.GetLocal (), rvector ({3},{1}));//I->J(TCP)

  staticRoutingC->AddHostRouteTo (Ipv4Address ("172.16.1.1"), fromLocal, rvector ({1},{1}));//C->A(normal packet)
  staticRoutingC->AddHostRouteTo (Ipv4Address ("172.16.1.1"), iAC.GetAddress (1,0), rvector ({1},{1}));//C->A(TCP)
  staticRoutingC->AddHostRouteTo (Ipv4Address ("172.16.1.1"), ifInAddrC.GetLocal (), rvector ({1},{1}));//C->A(TCP)
  staticRoutingC->AddHostRouteTo (iAB.GetAddress (0,0), ifInAddrC.GetLocal (), rvector ({1},{1}));//C->A(TCP)
  staticRoutingC->AddHostRouteTo (iAC.GetAddress (0,0), ifInAddrC.GetLocal (), rvector ({1},{1}));//C->A(TCP)

  staticRoutingC->AddHostRouteTo (Ipv4Address ("172.16.1.2"), fromLocal, rvector ({2},{1}));//C->B(normal packet)
  staticRoutingC->AddHostRouteTo (Ipv4Address ("172.16.1.2"), iBC.GetAddress (1,0), rvector ({2},{1}));//C->B(TCP)
  staticRoutingC->AddHostRouteTo (Ipv4Address ("172.16.1.2"), ifInAddrC.GetLocal (), rvector ({2},{1}));//C->B(TCP)
  staticRoutingC->AddHostRouteTo (iAB.GetAddress (1,0), ifInAddrC.GetLocal (), rvector ({2},{1}));//C->B(TCP)
  staticRoutingC->AddHostRouteTo (iBC.GetAddress (0,0), ifInAddrC.GetLocal (), rvector ({2},{1}));//C->B(TCP)
  staticRoutingC->AddHostRouteTo (iBD.GetAddress (0,0), ifInAddrC.GetLocal (), rvector ({2},{1}));//C->B(TCP)

  staticRoutingC->AddHostRouteTo (Ipv4Address ("172.16.1.4"), fromLocal, rvector ({2},{1}));//C->B(normal packet)
  staticRoutingC->AddHostRouteTo (Ipv4Address ("172.16.1.4"), iBC.GetAddress (1,0), rvector ({2},{1}));//C->B(TCP)
  staticRoutingC->AddHostRouteTo (Ipv4Address ("172.16.1.4"), ifInAddrC.GetLocal (), rvector ({2},{1}));//C->B(TCP)
  staticRoutingC->AddHostRouteTo (iBD.GetAddress (1,0), ifInAddrC.GetLocal (), rvector ({2},{1}));//C->B(TCP)
  staticRoutingC->AddHostRouteTo (iDF.GetAddress (0,0), ifInAddrC.GetLocal (), rvector ({2},{1}));//C->B(TCP)
  staticRoutingB->AddHostRouteTo (Ipv4Address ("172.16.1.4"), iBC.GetAddress (1,0), rvector ({3},{1}));//B->D
  staticRoutingB->AddHostRouteTo (Ipv4Address ("172.16.1.4"), ifInAddrC.GetLocal (), rvector ({3},{1}));//B->D(TCP)
  staticRoutingB->AddHostRouteTo (iBD.GetAddress (1,0), ifInAddrC.GetLocal (), rvector ({3},{1}));//B->D(TCP)
  staticRoutingB->AddHostRouteTo (iDF.GetAddress (0,0), ifInAddrC.GetLocal (), rvector ({3},{1}));//B->D(TCP)

  staticRoutingC->AddHostRouteTo (Ipv4Address ("172.16.1.5"), fromLocal, rvector ({3},{1}));//C->E(normal packet)
  staticRoutingC->AddHostRouteTo (Ipv4Address ("172.16.1.5"), iCE.GetAddress (0,0), rvector ({3},{1}));//C->E(TCP)
  staticRoutingC->AddHostRouteTo (Ipv4Address ("172.16.1.5"), ifInAddrC.GetLocal (), rvector ({3},{1}));//C->E(TCP)
  staticRoutingC->AddHostRouteTo (iCE.GetAddress (1,0), ifInAddrC.GetLocal (), rvector ({3},{1}));//C->E(TCP)
  staticRoutingC->AddHostRouteTo (iEF.GetAddress (0,0), ifInAddrC.GetLocal (), rvector ({3},{1}));//C->E(TCP)
  staticRoutingC->AddHostRouteTo (iEH.GetAddress (0,0), ifInAddrC.GetLocal (), rvector ({3},{1}));//C->E(TCP)

  staticRoutingC->AddHostRouteTo (Ipv4Address ("172.16.1.6"), fromLocal, rvector ({3},{1}));//C->E(normal packet)
  staticRoutingC->AddHostRouteTo (Ipv4Address ("172.16.1.6"), iCE.GetAddress (0,0), rvector ({3},{1}));//C->E(TCP)
  staticRoutingC->AddHostRouteTo (Ipv4Address ("172.16.1.6"), ifInAddrC.GetLocal (), rvector ({3},{1}));//C->E(TCP)
  staticRoutingC->AddHostRouteTo (iEF.GetAddress (1,0), ifInAddrC.GetLocal (), rvector ({3},{1}));//C->E(TCP)
  staticRoutingC->AddHostRouteTo (iDF.GetAddress (1,0), ifInAddrC.GetLocal (), rvector ({3},{1}));//C->E(TCP)
  staticRoutingC->AddHostRouteTo (iFI.GetAddress (0,0), ifInAddrC.GetLocal (), rvector ({3},{1}));//C->E(TCP)
  staticRoutingE->AddHostRouteTo (Ipv4Address ("172.16.1.6"), iCE.GetAddress (0,0), rvector ({2},{1}));//E->F
  staticRoutingE->AddHostRouteTo (Ipv4Address ("172.16.1.6"), ifInAddrC.GetLocal (), rvector ({2},{1}));//E->F(TCP)
  staticRoutingE->AddHostRouteTo (iEF.GetAddress (1,0), ifInAddrC.GetLocal (), rvector ({2},{1}));//E->F(TCP)
  staticRoutingE->AddHostRouteTo (iDF.GetAddress (1,0), ifInAddrC.GetLocal (), rvector ({2},{1}));//E->F(TCP)
  staticRoutingE->AddHostRouteTo (iFI.GetAddress (0,0), ifInAddrC.GetLocal (), rvector ({2},{1}));//E->F(TCP)

  staticRoutingC->AddHostRouteTo (Ipv4Address ("172.16.1.7"), fromLocal, rvector ({3},{1}));//C->E(normal packet)
  staticRoutingC->AddHostRouteTo (Ipv4Address ("172.16.1.7"), iCE.GetAddress (0,0), rvector ({3},{1}));//C->E(TCP)
  staticRoutingC->AddHostRouteTo (Ipv4Address ("172.16.1.7"), ifInAddrC.GetLocal (), rvector ({3},{1}));//C->E(TCP)
  staticRoutingC->AddHostRouteTo (iGH.GetAddress (0,0), ifInAddrC.GetLocal (), rvector ({3},{1}));//C->E(TCP)
  staticRoutingC->AddHostRouteTo (iGK.GetAddress (0,0), ifInAddrC.GetLocal (), rvector ({3},{1}));//C->E(TCP)
  staticRoutingE->AddHostRouteTo (Ipv4Address ("172.16.1.7"), iCE.GetAddress (0,0), rvector ({3},{1}));//E->H
  staticRoutingE->AddHostRouteTo (Ipv4Address ("172.16.1.7"), ifInAddrC.GetLocal (), rvector ({3},{1}));//E->H(TCP)
  staticRoutingE->AddHostRouteTo (iGH.GetAddress (0,0), ifInAddrC.GetLocal (), rvector ({3},{1}));//E->H(TCP)
  staticRoutingE->AddHostRouteTo (iGK.GetAddress (0,0), ifInAddrC.GetLocal (), rvector ({3},{1}));//E->H(TCP)
  staticRoutingH->AddHostRouteTo (Ipv4Address ("172.16.1.7"), iCE.GetAddress (0,0), rvector ({2},{1}));//H->G
  staticRoutingH->AddHostRouteTo (Ipv4Address ("172.16.1.7"), ifInAddrC.GetLocal (), rvector ({2},{1}));//H->G(TCP)
  staticRoutingH->AddHostRouteTo (iGH.GetAddress (0,0), ifInAddrC.GetLocal (), rvector ({2},{1}));//H->G(TCP)
  staticRoutingH->AddHostRouteTo (iGK.GetAddress (0,0), ifInAddrC.GetLocal (), rvector ({2},{1}));//H->G(TCP)

  staticRoutingC->AddHostRouteTo (Ipv4Address ("172.16.1.8"), fromLocal, rvector ({3},{1}));//C->E(normal packet)
  staticRoutingC->AddHostRouteTo (Ipv4Address ("172.16.1.8"), iCE.GetAddress (0,0), rvector ({3},{1}));//C->E(TCP)
  staticRoutingC->AddHostRouteTo (Ipv4Address ("172.16.1.8"), ifInAddrC.GetLocal (), rvector ({3},{1}));//C->E(TCP)
  staticRoutingC->AddHostRouteTo (iEH.GetAddress (1,0), ifInAddrC.GetLocal (), rvector ({3},{1}));//C->E(TCP)
  staticRoutingC->AddHostRouteTo (iGH.GetAddress (1,0), ifInAddrC.GetLocal (), rvector ({3},{1}));//C->E(TCP)
  staticRoutingC->AddHostRouteTo (iHI.GetAddress (0,0), ifInAddrC.GetLocal (), rvector ({3},{1}));//C->E(TCP)
  staticRoutingE->AddHostRouteTo (Ipv4Address ("172.16.1.8"), iCE.GetAddress (0,0), rvector ({3},{1}));//E->H
  staticRoutingE->AddHostRouteTo (Ipv4Address ("172.16.1.8"), ifInAddrC.GetLocal (), rvector ({3},{1}));//E->H(TCP)
  staticRoutingE->AddHostRouteTo (iEH.GetAddress (1,0), ifInAddrC.GetLocal (), rvector ({3},{1}));//E->H(TCP)
  staticRoutingE->AddHostRouteTo (iGH.GetAddress (1,0), ifInAddrC.GetLocal (), rvector ({3},{1}));//E->H(TCP)
  staticRoutingE->AddHostRouteTo (iHI.GetAddress (0,0), ifInAddrC.GetLocal (), rvector ({3},{1}));//E->H(TCP)

  staticRoutingC->AddHostRouteTo (Ipv4Address ("172.16.1.9"), fromLocal, rvector ({3},{1}));//C->E(normal packet)
  staticRoutingC->AddHostRouteTo (Ipv4Address ("172.16.1.9"), iCE.GetAddress (0,0), rvector ({3},{1}));//C->E(TCP)
  staticRoutingC->AddHostRouteTo (Ipv4Address ("172.16.1.9"), ifInAddrC.GetLocal (), rvector ({3},{1}));//C->E(TCP)
  staticRoutingC->AddHostRouteTo (iFI.GetAddress (1,0), ifInAddrC.GetLocal (), rvector ({3},{1}));//C->E(TCP)
  staticRoutingC->AddHostRouteTo (iHI.GetAddress (1,0), ifInAddrC.GetLocal (), rvector ({3},{1}));//C->E(TCP)
  staticRoutingC->AddHostRouteTo (iIJ.GetAddress (0,0), ifInAddrC.GetLocal (), rvector ({3},{1}));//C->E(TCP)
  staticRoutingE->AddHostRouteTo (Ipv4Address ("172.16.1.9"), iCE.GetAddress (0,0), rvector ({3},{1}));//E->H
  staticRoutingE->AddHostRouteTo (Ipv4Address ("172.16.1.9"), ifInAddrC.GetLocal (), rvector ({3},{1}));//E->H(TCP)
  staticRoutingE->AddHostRouteTo (iFI.GetAddress (1,0), ifInAddrC.GetLocal (), rvector ({3},{1}));//E->H(TCP)
  staticRoutingE->AddHostRouteTo (iHI.GetAddress (1,0), ifInAddrC.GetLocal (), rvector ({3},{1}));//E->H(TCP)
  staticRoutingE->AddHostRouteTo (iIJ.GetAddress (0,0), ifInAddrC.GetLocal (), rvector ({3},{1}));//E->H(TCP)
  staticRoutingH->AddHostRouteTo (Ipv4Address ("172.16.1.9"), iCE.GetAddress (0,0), rvector ({3},{1}));//H->I
  staticRoutingH->AddHostRouteTo (Ipv4Address ("172.16.1.9"), ifInAddrC.GetLocal (), rvector ({3},{1}));//H->I(TCP)
  staticRoutingH->AddHostRouteTo (iFI.GetAddress (1,0), ifInAddrC.GetLocal (), rvector ({3},{1}));//H->I(TCP)
  staticRoutingH->AddHostRouteTo (iHI.GetAddress (1,0), ifInAddrC.GetLocal (), rvector ({3},{1}));//H->I(TCP)
  staticRoutingH->AddHostRouteTo (iIJ.GetAddress (0,0), ifInAddrC.GetLocal (), rvector ({3},{1}));//H->I(TCP)

  staticRoutingC->AddHostRouteTo (Ipv4Address ("172.16.1.10"), fromLocal, rvector ({3},{1}));//C->E(normal packet)
  staticRoutingC->AddHostRouteTo (Ipv4Address ("172.16.1.10"), iCE.GetAddress (0,0), rvector ({3},{1}));//C->E(TCP)
  staticRoutingC->AddHostRouteTo (Ipv4Address ("172.16.1.10"), ifInAddrC.GetLocal (), rvector ({3},{1}));//C->E(TCP)
  staticRoutingC->AddHostRouteTo (iIJ.GetAddress (1,0), ifInAddrC.GetLocal (), rvector ({3},{1}));//C->E(TCP)
  staticRoutingC->AddHostRouteTo (iJK.GetAddress (0,0), ifInAddrC.GetLocal (), rvector ({3},{1}));//C->E(TCP)
  staticRoutingE->AddHostRouteTo (Ipv4Address ("172.16.1.10"), iCE.GetAddress (0,0), rvector ({3},{1}));//E->H
  staticRoutingE->AddHostRouteTo (Ipv4Address ("172.16.1.10"), ifInAddrC.GetLocal (), rvector ({3},{1}));//E->H(TCP)
  staticRoutingE->AddHostRouteTo (iIJ.GetAddress (1,0), ifInAddrC.GetLocal (), rvector ({3},{1}));//E->H(TCP)
  staticRoutingE->AddHostRouteTo (iJK.GetAddress (0,0), ifInAddrC.GetLocal (), rvector ({3},{1}));//E->H(TCP)
  staticRoutingH->AddHostRouteTo (Ipv4Address ("172.16.1.10"), iCE.GetAddress (0,0), rvector ({3},{1}));//H->I
  staticRoutingH->AddHostRouteTo (Ipv4Address ("172.16.1.10"), ifInAddrC.GetLocal (), rvector ({3},{1}));//H->I(TCP)
  staticRoutingH->AddHostRouteTo (iIJ.GetAddress (1,0), ifInAddrC.GetLocal (), rvector ({3},{1}));//H->I(TCP)
  staticRoutingH->AddHostRouteTo (iJK.GetAddress (0,0), ifInAddrC.GetLocal (), rvector ({3},{1}));//H->I(TCP)
  staticRoutingI->AddHostRouteTo (Ipv4Address ("172.16.1.10"), iCE.GetAddress (0,0), rvector ({3},{1}));//I->J
  staticRoutingI->AddHostRouteTo (Ipv4Address ("172.16.1.10"), ifInAddrC.GetLocal (), rvector ({3},{1}));//I->J(TCP)
  staticRoutingI->AddHostRouteTo (iIJ.GetAddress (1,0), ifInAddrC.GetLocal (), rvector ({3},{1}));//I->J(TCP)
  staticRoutingI->AddHostRouteTo (iJK.GetAddress (0,0), ifInAddrC.GetLocal (), rvector ({3},{1}));//I->J(TCP)

  staticRoutingC->AddHostRouteTo (Ipv4Address ("172.16.1.11"), fromLocal, rvector ({3},{1}));//C->E(normal packet)
  staticRoutingC->AddHostRouteTo (Ipv4Address ("172.16.1.11"), iCE.GetAddress (0,0), rvector ({3},{1}));//C->E(TCP)
  staticRoutingC->AddHostRouteTo (Ipv4Address ("172.16.1.11"), ifInAddrC.GetLocal (), rvector ({3},{1}));//C->E(TCP)
  staticRoutingC->AddHostRouteTo (iGK.GetAddress (1,0), ifInAddrC.GetLocal (), rvector ({3},{1}));//C->E(TCP)
  staticRoutingC->AddHostRouteTo (iJK.GetAddress (1,0), ifInAddrC.GetLocal (), rvector ({3},{1}));//C->E(TCP)
  staticRoutingE->AddHostRouteTo (Ipv4Address ("172.16.1.11"), iCE.GetAddress (0,0), rvector ({3},{1}));//E->H
  staticRoutingE->AddHostRouteTo (Ipv4Address ("172.16.1.11"), ifInAddrC.GetLocal (), rvector ({3},{1}));//E->H(TCP)
  staticRoutingE->AddHostRouteTo (iGK.GetAddress (1,0), ifInAddrC.GetLocal (), rvector ({3},{1}));//E->H(TCP)
  staticRoutingE->AddHostRouteTo (iJK.GetAddress (1,0), ifInAddrC.GetLocal (), rvector ({3},{1}));//E->H(TCP)
  staticRoutingH->AddHostRouteTo (Ipv4Address ("172.16.1.11"), iCE.GetAddress (0,0), rvector ({2},{1}));//H->G
  staticRoutingH->AddHostRouteTo (Ipv4Address ("172.16.1.11"), ifInAddrC.GetLocal (), rvector ({2},{1}));//H->G(TCP)
  staticRoutingH->AddHostRouteTo (iGK.GetAddress (1,0), ifInAddrC.GetLocal (), rvector ({2},{1}));//H->G(TCP)
  staticRoutingH->AddHostRouteTo (iJK.GetAddress (1,0), ifInAddrC.GetLocal (), rvector ({2},{1}));//H->G(TCP)
  staticRoutingG->AddHostRouteTo (Ipv4Address ("172.16.1.11"), iCE.GetAddress (0,0), rvector ({2},{1}));//G->K
  staticRoutingG->AddHostRouteTo (Ipv4Address ("172.16.1.11"), ifInAddrC.GetLocal (), rvector ({2},{1}));//G->K(TCP)
  staticRoutingG->AddHostRouteTo (iGK.GetAddress (1,0), ifInAddrC.GetLocal (), rvector ({2},{1}));//G->K(TCP)
  staticRoutingG->AddHostRouteTo (iJK.GetAddress (1,0), ifInAddrC.GetLocal (), rvector ({2},{1}));//G->K(TCP)

  staticRoutingD->AddHostRouteTo (Ipv4Address ("172.16.1.1"), fromLocal, rvector ({1},{1}));//D->B(normal packet)
  staticRoutingD->AddHostRouteTo (Ipv4Address ("172.16.1.1"), iBD.GetAddress (1,0), rvector ({1},{1}));//D->B(TCP)
  staticRoutingD->AddHostRouteTo (Ipv4Address ("172.16.1.1"), ifInAddrD.GetLocal (), rvector ({1},{1}));//D->B(TCP)
  staticRoutingD->AddHostRouteTo (iAB.GetAddress (0,0), ifInAddrD.GetLocal (), rvector ({1},{1}));//D->B(TCP)
  staticRoutingD->AddHostRouteTo (iAC.GetAddress (0,0), ifInAddrD.GetLocal (), rvector ({1},{1}));//D->B(TCP)
  staticRoutingB->AddHostRouteTo (Ipv4Address ("172.16.1.1"), iBD.GetAddress (1,0), rvector ({1},{1}));//B->A
  staticRoutingB->AddHostRouteTo (Ipv4Address ("172.16.1.1"), ifInAddrD.GetLocal (), rvector ({1},{1}));//B->A(TCP)
  staticRoutingB->AddHostRouteTo (iAB.GetAddress (0,0), ifInAddrD.GetLocal (), rvector ({1},{1}));//B->A(TCP)
  staticRoutingB->AddHostRouteTo (iAC.GetAddress (0,0), ifInAddrD.GetLocal (), rvector ({1},{1}));//B->A(TCP)

  staticRoutingD->AddHostRouteTo (Ipv4Address ("172.16.1.2"), fromLocal, rvector ({1},{1}));//D->B(normal packet)
  staticRoutingD->AddHostRouteTo (Ipv4Address ("172.16.1.2"), iBD.GetAddress (1,0), rvector ({1},{1}));//D->B(TCP)
  staticRoutingD->AddHostRouteTo (Ipv4Address ("172.16.1.2"), ifInAddrD.GetLocal (), rvector ({1},{1}));//D->B(TCP)
  staticRoutingD->AddHostRouteTo (iAB.GetAddress (1,0), ifInAddrD.GetLocal (), rvector ({1},{1}));//D->B(TCP)
  staticRoutingD->AddHostRouteTo (iBC.GetAddress (0,0), ifInAddrD.GetLocal (), rvector ({1},{1}));//D->B(TCP)
  staticRoutingD->AddHostRouteTo (iBD.GetAddress (0,0), ifInAddrD.GetLocal (), rvector ({1},{1}));//D->B(TCP)

  staticRoutingD->AddHostRouteTo (Ipv4Address ("172.16.1.3"), fromLocal, rvector ({1},{1}));//D->B(normal packet)
  staticRoutingD->AddHostRouteTo (Ipv4Address ("172.16.1.3"), iBD.GetAddress (1,0), rvector ({1},{1}));//D->B(TCP)
  staticRoutingD->AddHostRouteTo (Ipv4Address ("172.16.1.3"), ifInAddrD.GetLocal (), rvector ({1},{1}));//D->B(TCP)
  staticRoutingD->AddHostRouteTo (iAC.GetAddress (1,0), ifInAddrD.GetLocal (), rvector ({1},{1}));//D->B(TCP)
  staticRoutingD->AddHostRouteTo (iBC.GetAddress (1,0), ifInAddrD.GetLocal (), rvector ({1},{1}));//D->B(TCP)
  staticRoutingD->AddHostRouteTo (iCE.GetAddress (0,0), ifInAddrD.GetLocal (), rvector ({1},{1}));//D->B(TCP)
  staticRoutingB->AddHostRouteTo (Ipv4Address ("172.16.1.3"), iBD.GetAddress (1,0), rvector ({2},{1}));//B->C
  staticRoutingB->AddHostRouteTo (Ipv4Address ("172.16.1.3"), ifInAddrD.GetLocal (), rvector ({2},{1}));//B->C(TCP)
  staticRoutingB->AddHostRouteTo (iAC.GetAddress (1,0), ifInAddrD.GetLocal (), rvector ({2},{1}));//B->C(TCP)
  staticRoutingB->AddHostRouteTo (iBC.GetAddress (1,0), ifInAddrD.GetLocal (), rvector ({2},{1}));//B->C(TCP)
  staticRoutingB->AddHostRouteTo (iCE.GetAddress (0,0), ifInAddrD.GetLocal (), rvector ({2},{1}));//B->C(TCP)

  staticRoutingD->AddHostRouteTo (Ipv4Address ("172.16.1.5"), fromLocal, rvector ({2},{1}));//D->F(normal packet)
  staticRoutingD->AddHostRouteTo (Ipv4Address ("172.16.1.5"), iDF.GetAddress (0,0), rvector ({2},{1}));//D->F(TCP)
  staticRoutingD->AddHostRouteTo (Ipv4Address ("172.16.1.5"), ifInAddrD.GetLocal (), rvector ({2},{1}));//D->F(TCP)
  staticRoutingD->AddHostRouteTo (iCE.GetAddress (1,0), ifInAddrD.GetLocal (), rvector ({2},{1}));//D->F(TCP)
  staticRoutingD->AddHostRouteTo (iEF.GetAddress (0,0), ifInAddrD.GetLocal (), rvector ({2},{1}));//D->F(TCP)
  staticRoutingD->AddHostRouteTo (iEH.GetAddress (0,0), ifInAddrD.GetLocal (), rvector ({2},{1}));//D->F(TCP)
  staticRoutingF->AddHostRouteTo (Ipv4Address ("172.16.1.5"), iDF.GetAddress (0,0), rvector ({1},{1}));//F->E
  staticRoutingF->AddHostRouteTo (Ipv4Address ("172.16.1.5"), ifInAddrD.GetLocal (), rvector ({1},{1}));//F->E(TCP)
  staticRoutingF->AddHostRouteTo (iCE.GetAddress (1,0), ifInAddrD.GetLocal (), rvector ({1},{1}));//F->E(TCP)
  staticRoutingF->AddHostRouteTo (iEF.GetAddress (0,0), ifInAddrD.GetLocal (), rvector ({1},{1}));//F->E(TCP)
  staticRoutingF->AddHostRouteTo (iEH.GetAddress (0,0), ifInAddrD.GetLocal (), rvector ({1},{1}));//F->E(TCP)

  staticRoutingD->AddHostRouteTo (Ipv4Address ("172.16.1.6"), fromLocal, rvector ({2},{1}));//D->F(normal packet)
  staticRoutingD->AddHostRouteTo (Ipv4Address ("172.16.1.6"), iDF.GetAddress (0,0), rvector ({2},{1}));//D->F(TCP)
  staticRoutingD->AddHostRouteTo (Ipv4Address ("172.16.1.6"), ifInAddrD.GetLocal (), rvector ({2},{1}));//D->F(TCP)
  staticRoutingD->AddHostRouteTo (iEF.GetAddress (1,0), ifInAddrD.GetLocal (), rvector ({2},{1}));//D->F(TCP)
  staticRoutingD->AddHostRouteTo (iDF.GetAddress (1,0), ifInAddrD.GetLocal (), rvector ({2},{1}));//D->F(TCP)
  staticRoutingD->AddHostRouteTo (iFI.GetAddress (0,0), ifInAddrD.GetLocal (), rvector ({2},{1}));//D->F(TCP)

  staticRoutingD->AddHostRouteTo (Ipv4Address ("172.16.1.7"), fromLocal, rvector ({2},{1}));//D->F(normal packet)
  staticRoutingD->AddHostRouteTo (Ipv4Address ("172.16.1.7"), iDF.GetAddress (0,0), rvector ({2},{1}));//D->F(TCP)
  staticRoutingD->AddHostRouteTo (Ipv4Address ("172.16.1.7"), ifInAddrD.GetLocal (), rvector ({2},{1}));//D->F(TCP)
  staticRoutingD->AddHostRouteTo (iGH.GetAddress (0,0), ifInAddrD.GetLocal (), rvector ({2},{1}));//D->F(TCP)
  staticRoutingD->AddHostRouteTo (iGK.GetAddress (0,0), ifInAddrD.GetLocal (), rvector ({2},{1}));//D->F(TCP)
  staticRoutingF->AddHostRouteTo (Ipv4Address ("172.16.1.7"), iDF.GetAddress (0,0), rvector ({1},{1}));//F->E
  staticRoutingF->AddHostRouteTo (Ipv4Address ("172.16.1.7"), ifInAddrD.GetLocal (), rvector ({1},{1}));//F->E(TCP)
  staticRoutingF->AddHostRouteTo (iGH.GetAddress (0,0), ifInAddrD.GetLocal (), rvector ({1},{1}));//F->E(TCP)
  staticRoutingF->AddHostRouteTo (iGK.GetAddress (0,0), ifInAddrD.GetLocal (), rvector ({1},{1}));//F->E(TCP)
  staticRoutingE->AddHostRouteTo (Ipv4Address ("172.16.1.7"), iDF.GetAddress (0,0), rvector ({3},{1}));//E->H
  staticRoutingE->AddHostRouteTo (Ipv4Address ("172.16.1.7"), ifInAddrD.GetLocal (), rvector ({3},{1}));//E->H(TCP)
  staticRoutingE->AddHostRouteTo (iGH.GetAddress (0,0), ifInAddrD.GetLocal (), rvector ({3},{1}));//E->H(TCP)
  staticRoutingE->AddHostRouteTo (iGK.GetAddress (0,0), ifInAddrD.GetLocal (), rvector ({3},{1}));//E->H(TCP)
  staticRoutingH->AddHostRouteTo (Ipv4Address ("172.16.1.7"), iDF.GetAddress (0,0), rvector ({2},{1}));//H->G
  staticRoutingH->AddHostRouteTo (Ipv4Address ("172.16.1.7"), ifInAddrD.GetLocal (), rvector ({2},{1}));//H->G(TCP)
  staticRoutingH->AddHostRouteTo (iGH.GetAddress (0,0), ifInAddrD.GetLocal (), rvector ({2},{1}));//H->G(TCP)
  staticRoutingH->AddHostRouteTo (iGK.GetAddress (0,0), ifInAddrD.GetLocal (), rvector ({2},{1}));//H->G(TCP)

  staticRoutingD->AddHostRouteTo (Ipv4Address ("172.16.1.8"), fromLocal, rvector ({2},{1}));//D->F(normal packet)
  staticRoutingD->AddHostRouteTo (Ipv4Address ("172.16.1.8"), iDF.GetAddress (0,0), rvector ({2},{1}));//D->F(TCP)
  staticRoutingD->AddHostRouteTo (Ipv4Address ("172.16.1.8"), ifInAddrD.GetLocal (), rvector ({2},{1}));//D->F(TCP)
  staticRoutingD->AddHostRouteTo (iEH.GetAddress (1,0), ifInAddrD.GetLocal (), rvector ({2},{1}));//D->F(TCP)
  staticRoutingD->AddHostRouteTo (iGH.GetAddress (1,0), ifInAddrD.GetLocal (), rvector ({2},{1}));//D->F(TCP)
  staticRoutingD->AddHostRouteTo (iHI.GetAddress (0,0), ifInAddrD.GetLocal (), rvector ({2},{1}));//D->F(TCP)
  staticRoutingF->AddHostRouteTo (Ipv4Address ("172.16.1.8"), iDF.GetAddress (0,0), rvector ({1},{1}));//F->E
  staticRoutingF->AddHostRouteTo (Ipv4Address ("172.16.1.8"), ifInAddrD.GetLocal (), rvector ({1},{1}));//F->E(TCP)
  staticRoutingF->AddHostRouteTo (iEH.GetAddress (1,0), ifInAddrD.GetLocal (), rvector ({1},{1}));//F->E(TCP)
  staticRoutingF->AddHostRouteTo (iGH.GetAddress (1,0), ifInAddrD.GetLocal (), rvector ({1},{1}));//F->E(TCP)
  staticRoutingF->AddHostRouteTo (iHI.GetAddress (0,0), ifInAddrD.GetLocal (), rvector ({1},{1}));//F->E(TCP)
  staticRoutingE->AddHostRouteTo (Ipv4Address ("172.16.1.8"), iDF.GetAddress (0,0), rvector ({3},{1}));//E->H
  staticRoutingE->AddHostRouteTo (Ipv4Address ("172.16.1.8"), ifInAddrD.GetLocal (), rvector ({3},{1}));//E->H(TCP)
  staticRoutingE->AddHostRouteTo (iEH.GetAddress (1,0), ifInAddrD.GetLocal (), rvector ({3},{1}));//E->H(TCP)
  staticRoutingE->AddHostRouteTo (iGH.GetAddress (1,0), ifInAddrD.GetLocal (), rvector ({3},{1}));//E->H(TCP)
  staticRoutingE->AddHostRouteTo (iHI.GetAddress (0,0), ifInAddrD.GetLocal (), rvector ({3},{1}));//E->H(TCP)

  staticRoutingD->AddHostRouteTo (Ipv4Address ("172.16.1.9"), fromLocal, rvector ({2},{1}));//D->F(normal packet)
  staticRoutingD->AddHostRouteTo (Ipv4Address ("172.16.1.9"), iDF.GetAddress (0,0), rvector ({2},{1}));//D->F(TCP)
  staticRoutingD->AddHostRouteTo (Ipv4Address ("172.16.1.9"), ifInAddrD.GetLocal (), rvector ({2},{1}));//D->F(TCP)
  staticRoutingD->AddHostRouteTo (iFI.GetAddress (1,0), ifInAddrD.GetLocal (), rvector ({2},{1}));//D->F(TCP)
  staticRoutingD->AddHostRouteTo (iHI.GetAddress (1,0), ifInAddrD.GetLocal (), rvector ({2},{1}));//D->F(TCP)
  staticRoutingD->AddHostRouteTo (iIJ.GetAddress (0,0), ifInAddrD.GetLocal (), rvector ({2},{1}));//D->F(TCP)
  staticRoutingF->AddHostRouteTo (Ipv4Address ("172.16.1.9"), iDF.GetAddress (0,0), rvector ({3},{1}));//F->I
  staticRoutingF->AddHostRouteTo (Ipv4Address ("172.16.1.9"), ifInAddrD.GetLocal (), rvector ({3},{1}));//F->I(TCP)
  staticRoutingF->AddHostRouteTo (iFI.GetAddress (1,0), ifInAddrD.GetLocal (), rvector ({3},{1}));//F->I(TCP)
  staticRoutingF->AddHostRouteTo (iHI.GetAddress (1,0), ifInAddrD.GetLocal (), rvector ({3},{1}));//F->I(TCP)
  staticRoutingF->AddHostRouteTo (iIJ.GetAddress (0,0), ifInAddrD.GetLocal (), rvector ({3},{1}));//F->I(TCP)

  staticRoutingD->AddHostRouteTo (Ipv4Address ("172.16.1.10"), fromLocal, rvector ({2},{1}));//D->F(normal packet)
  staticRoutingD->AddHostRouteTo (Ipv4Address ("172.16.1.10"), iDF.GetAddress (0,0), rvector ({2},{1}));//D->F(TCP)
  staticRoutingD->AddHostRouteTo (Ipv4Address ("172.16.1.10"), ifInAddrD.GetLocal (), rvector ({2},{1}));//D->F(TCP)
  staticRoutingD->AddHostRouteTo (iIJ.GetAddress (1,0), ifInAddrD.GetLocal (), rvector ({2},{1}));//D->F(TCP)
  staticRoutingD->AddHostRouteTo (iJK.GetAddress (0,0), ifInAddrD.GetLocal (), rvector ({2},{1}));//D->F(TCP)
  staticRoutingF->AddHostRouteTo (Ipv4Address ("172.16.1.10"), iDF.GetAddress (0,0), rvector ({3},{1}));//F->I
  staticRoutingF->AddHostRouteTo (Ipv4Address ("172.16.1.10"), ifInAddrD.GetLocal (), rvector ({3},{1}));//F->I(TCP)
  staticRoutingF->AddHostRouteTo (iIJ.GetAddress (1,0), ifInAddrD.GetLocal (), rvector ({3},{1}));//F->I(TCP)
  staticRoutingF->AddHostRouteTo (iJK.GetAddress (0,0), ifInAddrD.GetLocal (), rvector ({3},{1}));//F->I(TCP)
  staticRoutingI->AddHostRouteTo (Ipv4Address ("172.16.1.10"), iDF.GetAddress (0,0), rvector ({3},{1}));//I->J
  staticRoutingI->AddHostRouteTo (Ipv4Address ("172.16.1.10"), ifInAddrD.GetLocal (), rvector ({3},{1}));//I->J(TCP)
  staticRoutingI->AddHostRouteTo (iIJ.GetAddress (1,0), ifInAddrD.GetLocal (), rvector ({3},{1}));//I->J(TCP)
  staticRoutingI->AddHostRouteTo (iJK.GetAddress (0,0), ifInAddrD.GetLocal (), rvector ({3},{1}));//I->J(TCP)

  staticRoutingD->AddHostRouteTo (Ipv4Address ("172.16.1.11"), fromLocal, rvector ({2},{1}));//D->F(normal packet)
  staticRoutingD->AddHostRouteTo (Ipv4Address ("172.16.1.11"), iDF.GetAddress (0,0), rvector ({2},{1}));//D->F(TCP)
  staticRoutingD->AddHostRouteTo (Ipv4Address ("172.16.1.11"), ifInAddrD.GetLocal (), rvector ({2},{1}));//D->F(TCP)
  staticRoutingD->AddHostRouteTo (iGK.GetAddress (1,0), ifInAddrD.GetLocal (), rvector ({2},{1}));//D->F(TCP)
  staticRoutingD->AddHostRouteTo (iJK.GetAddress (1,0), ifInAddrD.GetLocal (), rvector ({2},{1}));//D->F(TCP)
  staticRoutingF->AddHostRouteTo (Ipv4Address ("172.16.1.11"), iDF.GetAddress (0,0), rvector ({3},{1}));//F->I
  staticRoutingF->AddHostRouteTo (Ipv4Address ("172.16.1.11"), ifInAddrD.GetLocal (), rvector ({3},{1}));//F->I(TCP)
  staticRoutingF->AddHostRouteTo (iGK.GetAddress (1,0), ifInAddrD.GetLocal (), rvector ({3},{1}));//F->I(TCP)
  staticRoutingF->AddHostRouteTo (iJK.GetAddress (1,0), ifInAddrD.GetLocal (), rvector ({3},{1}));//F->I(TCP)
  staticRoutingI->AddHostRouteTo (Ipv4Address ("172.16.1.11"), iDF.GetAddress (0,0), rvector ({3},{1}));//I->J
  staticRoutingI->AddHostRouteTo (Ipv4Address ("172.16.1.11"), ifInAddrD.GetLocal (), rvector ({3},{1}));//I->J(TCP)
  staticRoutingI->AddHostRouteTo (iGK.GetAddress (1,0), ifInAddrD.GetLocal (), rvector ({3},{1}));//I->J(TCP)
  staticRoutingI->AddHostRouteTo (iJK.GetAddress (1,0), ifInAddrD.GetLocal (), rvector ({3},{1}));//I->J(TCP)
  staticRoutingJ->AddHostRouteTo (Ipv4Address ("172.16.1.11"), iDF.GetAddress (0,0), rvector ({2},{1}));//J->K
  staticRoutingJ->AddHostRouteTo (Ipv4Address ("172.16.1.11"), ifInAddrD.GetLocal (), rvector ({2},{1}));//J->K(TCP)
  staticRoutingJ->AddHostRouteTo (iGK.GetAddress (1,0), ifInAddrD.GetLocal (), rvector ({2},{1}));//J->K(TCP)
  staticRoutingJ->AddHostRouteTo (iJK.GetAddress (1,0), ifInAddrD.GetLocal (), rvector ({2},{1}));//J->K(TCP)

  staticRoutingE->AddHostRouteTo (Ipv4Address ("172.16.1.1"), fromLocal, rvector ({1},{1}));//E->C(normal packet)
  staticRoutingE->AddHostRouteTo (Ipv4Address ("172.16.1.1"), iCE.GetAddress (1,0), rvector ({1},{1}));//E->C(TCP)
  staticRoutingE->AddHostRouteTo (Ipv4Address ("172.16.1.1"), ifInAddrE.GetLocal (), rvector ({1},{1}));//E->C(TCP)
  staticRoutingE->AddHostRouteTo (iAB.GetAddress (0,0), ifInAddrE.GetLocal (), rvector ({1},{1}));//E->C(TCP)
  staticRoutingE->AddHostRouteTo (iAC.GetAddress (0,0), ifInAddrE.GetLocal (), rvector ({1},{1}));//E->C(TCP)
  staticRoutingC->AddHostRouteTo (Ipv4Address ("172.16.1.1"), iCE.GetAddress (1,0), rvector ({1},{1}));//C->A
  staticRoutingC->AddHostRouteTo (Ipv4Address ("172.16.1.1"), ifInAddrE.GetLocal (), rvector ({1},{1}));//C->A(TCP)
  staticRoutingC->AddHostRouteTo (iAB.GetAddress (0,0), ifInAddrE.GetLocal (), rvector ({1},{1}));//C->A(TCP)
  staticRoutingC->AddHostRouteTo (iAC.GetAddress (0,0), ifInAddrE.GetLocal (), rvector ({1},{1}));//C->A(TCP)

  staticRoutingE->AddHostRouteTo (Ipv4Address ("172.16.1.2"), fromLocal, rvector ({1},{1}));//E->C(normal packet)
  staticRoutingE->AddHostRouteTo (Ipv4Address ("172.16.1.2"), iCE.GetAddress (1,0), rvector ({1},{1}));//E->C(TCP)
  staticRoutingE->AddHostRouteTo (Ipv4Address ("172.16.1.2"), ifInAddrE.GetLocal (), rvector ({1},{1}));//E->C(TCP)
  staticRoutingE->AddHostRouteTo (iAB.GetAddress (1,0), ifInAddrE.GetLocal (), rvector ({1},{1}));//E->C(TCP)
  staticRoutingE->AddHostRouteTo (iBC.GetAddress (0,0), ifInAddrE.GetLocal (), rvector ({1},{1}));//E->C(TCP)
  staticRoutingE->AddHostRouteTo (iBD.GetAddress (0,0), ifInAddrE.GetLocal (), rvector ({1},{1}));//E->C(TCP)
  staticRoutingC->AddHostRouteTo (Ipv4Address ("172.16.1.2"), iCE.GetAddress (1,0), rvector ({2},{1}));//C->B
  staticRoutingC->AddHostRouteTo (Ipv4Address ("172.16.1.2"), ifInAddrE.GetLocal (), rvector ({2},{1}));//C->B(TCP)
  staticRoutingC->AddHostRouteTo (iAB.GetAddress (1,0), ifInAddrE.GetLocal (), rvector ({2},{1}));//C->B(TCP)
  staticRoutingC->AddHostRouteTo (iBC.GetAddress (0,0), ifInAddrE.GetLocal (), rvector ({2},{1}));//C->B(TCP)
  staticRoutingC->AddHostRouteTo (iBD.GetAddress (0,0), ifInAddrE.GetLocal (), rvector ({2},{1}));//C->B(TCP)

  staticRoutingE->AddHostRouteTo (Ipv4Address ("172.16.1.3"), fromLocal, rvector ({1},{1}));//E->C(normal packet)
  staticRoutingE->AddHostRouteTo (Ipv4Address ("172.16.1.3"), iCE.GetAddress (1,0), rvector ({1},{1}));//E->C(TCP)
  staticRoutingE->AddHostRouteTo (Ipv4Address ("172.16.1.3"), ifInAddrE.GetLocal (), rvector ({1},{1}));//E->C(TCP)
  staticRoutingE->AddHostRouteTo (iAC.GetAddress (1,0), ifInAddrE.GetLocal (), rvector ({1},{1}));//E->C(TCP)
  staticRoutingE->AddHostRouteTo (iBC.GetAddress (1,0), ifInAddrE.GetLocal (), rvector ({1},{1}));//E->C(TCP)
  staticRoutingE->AddHostRouteTo (iCE.GetAddress (0,0), ifInAddrE.GetLocal (), rvector ({1},{1}));//E->C(TCP)

  staticRoutingE->AddHostRouteTo (Ipv4Address ("172.16.1.6"), fromLocal, rvector ({2},{1}));//E->F(normal packet)
  staticRoutingE->AddHostRouteTo (Ipv4Address ("172.16.1.6"), iEF.GetAddress (0,0), rvector ({2},{1}));//E->F(TCP)
  staticRoutingE->AddHostRouteTo (Ipv4Address ("172.16.1.6"), ifInAddrE.GetLocal (), rvector ({2},{1}));//E->F(TCP)
  staticRoutingE->AddHostRouteTo (iEF.GetAddress (1,0), ifInAddrE.GetLocal (), rvector ({2},{1}));//E->F(TCP)
  staticRoutingE->AddHostRouteTo (iDF.GetAddress (1,0), ifInAddrE.GetLocal (), rvector ({2},{1}));//E->F(TCP)
  staticRoutingE->AddHostRouteTo (iFI.GetAddress (0,0), ifInAddrE.GetLocal (), rvector ({2},{1}));//E->F(TCP)

  staticRoutingE->AddHostRouteTo (Ipv4Address ("172.16.1.7"), fromLocal, rvector ({3},{1}));//E->H(normal packet)
  staticRoutingE->AddHostRouteTo (Ipv4Address ("172.16.1.7"), iEH.GetAddress (0,0), rvector ({3},{1}));//E->H(TCP)
  staticRoutingE->AddHostRouteTo (Ipv4Address ("172.16.1.7"), ifInAddrE.GetLocal (), rvector ({3},{1}));//E->H(TCP)
  staticRoutingE->AddHostRouteTo (iGH.GetAddress (0,0), ifInAddrE.GetLocal (), rvector ({3},{1}));//E->H(TCP)
  staticRoutingE->AddHostRouteTo (iGK.GetAddress (0,0), ifInAddrE.GetLocal (), rvector ({3},{1}));//E->H(TCP)
  staticRoutingH->AddHostRouteTo (Ipv4Address ("172.16.1.7"), iEH.GetAddress (0,0), rvector ({2},{1}));//H->G
  staticRoutingH->AddHostRouteTo (Ipv4Address ("172.16.1.7"), ifInAddrE.GetLocal (), rvector ({2},{1}));//H->G(TCP)
  staticRoutingH->AddHostRouteTo (iGH.GetAddress (0,0), ifInAddrE.GetLocal (), rvector ({2},{1}));//H->G(TCP)
  staticRoutingH->AddHostRouteTo (iGK.GetAddress (0,0), ifInAddrE.GetLocal (), rvector ({2},{1}));//H->G(TCP)

  staticRoutingE->AddHostRouteTo (Ipv4Address ("172.16.1.8"), fromLocal, rvector ({3},{1}));//E->H(normal packet)
  staticRoutingE->AddHostRouteTo (Ipv4Address ("172.16.1.8"), iEH.GetAddress (0,0), rvector ({3},{1}));//E->H(TCP)
  staticRoutingE->AddHostRouteTo (Ipv4Address ("172.16.1.8"), ifInAddrE.GetLocal (), rvector ({3},{1}));//E->H(TCP)
  staticRoutingE->AddHostRouteTo (iEH.GetAddress (1,0), ifInAddrE.GetLocal (), rvector ({3},{1}));//E->H(TCP)
  staticRoutingE->AddHostRouteTo (iGH.GetAddress (1,0), ifInAddrE.GetLocal (), rvector ({3},{1}));//E->H(TCP)
  staticRoutingE->AddHostRouteTo (iHI.GetAddress (0,0), ifInAddrE.GetLocal (), rvector ({3},{1}));//E->H(TCP)

  staticRoutingE->AddHostRouteTo (Ipv4Address ("172.16.1.9"), fromLocal, rvector ({2},{1}));//E->F(normal packet)
  staticRoutingE->AddHostRouteTo (Ipv4Address ("172.16.1.9"), iEF.GetAddress (0,0), rvector ({2},{1}));//E->F(TCP)
  staticRoutingE->AddHostRouteTo (Ipv4Address ("172.16.1.9"), ifInAddrE.GetLocal (), rvector ({2},{1}));//E->F(TCP)
  staticRoutingE->AddHostRouteTo (iFI.GetAddress (1,0), ifInAddrE.GetLocal (), rvector ({2},{1}));//E->F(TCP)
  staticRoutingE->AddHostRouteTo (iHI.GetAddress (1,0), ifInAddrE.GetLocal (), rvector ({2},{1}));//E->F(TCP)
  staticRoutingE->AddHostRouteTo (iIJ.GetAddress (0,0), ifInAddrE.GetLocal (), rvector ({2},{1}));//E->F(TCP)
  staticRoutingF->AddHostRouteTo (Ipv4Address ("172.16.1.9"), iEF.GetAddress (0,0), rvector ({3},{1}));//F->I
  staticRoutingF->AddHostRouteTo (Ipv4Address ("172.16.1.9"), ifInAddrE.GetLocal (), rvector ({3},{1}));//F->I(TCP)
  staticRoutingF->AddHostRouteTo (iFI.GetAddress (1,0), ifInAddrE.GetLocal (), rvector ({3},{1}));//F->I(TCP)
  staticRoutingF->AddHostRouteTo (iHI.GetAddress (1,0), ifInAddrE.GetLocal (), rvector ({3},{1}));//F->I(TCP)
  staticRoutingF->AddHostRouteTo (iIJ.GetAddress (0,0), ifInAddrE.GetLocal (), rvector ({3},{1}));//F->I(TCP)

  staticRoutingE->AddHostRouteTo (Ipv4Address ("172.16.1.10"), fromLocal, rvector ({2},{1}));//E->F(normal packet)
  staticRoutingE->AddHostRouteTo (Ipv4Address ("172.16.1.10"), iEF.GetAddress (0,0), rvector ({2},{1}));//E->F(TCP)
  staticRoutingE->AddHostRouteTo (Ipv4Address ("172.16.1.10"), ifInAddrE.GetLocal (), rvector ({2},{1}));//E->F(TCP)
  staticRoutingE->AddHostRouteTo (iIJ.GetAddress (1,0), ifInAddrE.GetLocal (), rvector ({2},{1}));//E->F(TCP)
  staticRoutingE->AddHostRouteTo (iJK.GetAddress (0,0), ifInAddrE.GetLocal (), rvector ({2},{1}));//E->F(TCP)
  staticRoutingF->AddHostRouteTo (Ipv4Address ("172.16.1.10"), iEF.GetAddress (0,0), rvector ({3},{1}));//F->I
  staticRoutingF->AddHostRouteTo (Ipv4Address ("172.16.1.10"), ifInAddrE.GetLocal (), rvector ({3},{1}));//F->I(TCP)
  staticRoutingF->AddHostRouteTo (iIJ.GetAddress (1,0), ifInAddrE.GetLocal (), rvector ({3},{1}));//F->I(TCP)
  staticRoutingF->AddHostRouteTo (iJK.GetAddress (0,0), ifInAddrE.GetLocal (), rvector ({3},{1}));//F->I(TCP)
  staticRoutingI->AddHostRouteTo (Ipv4Address ("172.16.1.10"), iEF.GetAddress (0,0), rvector ({3},{1}));//I->J
  staticRoutingI->AddHostRouteTo (Ipv4Address ("172.16.1.10"), ifInAddrE.GetLocal (), rvector ({3},{1}));//I->J(TCP)
  staticRoutingI->AddHostRouteTo (iIJ.GetAddress (1,0), ifInAddrE.GetLocal (), rvector ({3},{1}));//I->J(TCP)
  staticRoutingI->AddHostRouteTo (iJK.GetAddress (0,0), ifInAddrE.GetLocal (), rvector ({3},{1}));//I->J(TCP)

  staticRoutingE->AddHostRouteTo (Ipv4Address ("172.16.1.11"), fromLocal, rvector ({3},{1}));//E->H(normal packet)
  staticRoutingE->AddHostRouteTo (Ipv4Address ("172.16.1.11"), iEH.GetAddress (0,0), rvector ({3},{1}));//E->H(TCP)
  staticRoutingE->AddHostRouteTo (Ipv4Address ("172.16.1.11"), ifInAddrE.GetLocal (), rvector ({3},{1}));//E->H(TCP)
  staticRoutingE->AddHostRouteTo (iGK.GetAddress (1,0), ifInAddrE.GetLocal (), rvector ({3},{1}));//E->H(TCP)
  staticRoutingE->AddHostRouteTo (iJK.GetAddress (1,0), ifInAddrE.GetLocal (), rvector ({3},{1}));//E->H(TCP)
  staticRoutingH->AddHostRouteTo (Ipv4Address ("172.16.1.11"), iEH.GetAddress (0,0), rvector ({2},{1}));//H->G
  staticRoutingH->AddHostRouteTo (Ipv4Address ("172.16.1.11"), ifInAddrE.GetLocal (), rvector ({2},{1}));//H->G(TCP)
  staticRoutingH->AddHostRouteTo (iGK.GetAddress (1,0), ifInAddrE.GetLocal (), rvector ({2},{1}));//H->G(TCP)
  staticRoutingH->AddHostRouteTo (iJK.GetAddress (1,0), ifInAddrE.GetLocal (), rvector ({2},{1}));//H->G(TCP)
  staticRoutingG->AddHostRouteTo (Ipv4Address ("172.16.1.11"), iEH.GetAddress (0,0), rvector ({2},{1}));//G->K
  staticRoutingG->AddHostRouteTo (Ipv4Address ("172.16.1.11"), ifInAddrE.GetLocal (), rvector ({2},{1}));//G->K(TCP)
  staticRoutingG->AddHostRouteTo (iGK.GetAddress (1,0), ifInAddrE.GetLocal (), rvector ({2},{1}));//G->K(TCP)
  staticRoutingG->AddHostRouteTo (iJK.GetAddress (1,0), ifInAddrE.GetLocal (), rvector ({2},{1}));//G->K(TCP)

  staticRoutingF->AddHostRouteTo (Ipv4Address ("172.16.1.1"), fromLocal, rvector ({2},{1}));//F->D(normal packet)
  staticRoutingF->AddHostRouteTo (Ipv4Address ("172.16.1.1"), iDF.GetAddress (1,0), rvector ({2},{1}));//F->D(TCP)
  staticRoutingF->AddHostRouteTo (Ipv4Address ("172.16.1.1"), ifInAddrF.GetLocal (), rvector ({2},{1}));//F->D(TCP)
  staticRoutingF->AddHostRouteTo (iAB.GetAddress (0,0), ifInAddrF.GetLocal (), rvector ({2},{1}));//F->D(TCP)
  staticRoutingF->AddHostRouteTo (iAC.GetAddress (0,0), ifInAddrF.GetLocal (), rvector ({2},{1}));//F->D(TCP)
  staticRoutingD->AddHostRouteTo (Ipv4Address ("172.16.1.1"), iDF.GetAddress (1,0), rvector ({1},{1}));//D->B
  staticRoutingD->AddHostRouteTo (Ipv4Address ("172.16.1.1"), ifInAddrF.GetLocal (), rvector ({1},{1}));//D->B(TCP)
  staticRoutingD->AddHostRouteTo (iAB.GetAddress (0,0), ifInAddrF.GetLocal (), rvector ({1},{1}));//D->B(TCP)
  staticRoutingD->AddHostRouteTo (iAC.GetAddress (0,0), ifInAddrF.GetLocal (), rvector ({1},{1}));//D->B(TCP)
  staticRoutingB->AddHostRouteTo (Ipv4Address ("172.16.1.1"), iDF.GetAddress (1,0), rvector ({1},{1}));//B->A
  staticRoutingB->AddHostRouteTo (Ipv4Address ("172.16.1.1"), ifInAddrF.GetLocal (), rvector ({1},{1}));//B->A(TCP)
  staticRoutingB->AddHostRouteTo (iAB.GetAddress (0,0), ifInAddrF.GetLocal (), rvector ({1},{1}));//B->A(TCP)
  staticRoutingB->AddHostRouteTo (iAC.GetAddress (0,0), ifInAddrF.GetLocal (), rvector ({1},{1}));//B->A(TCP)

  staticRoutingF->AddHostRouteTo (Ipv4Address ("172.16.1.2"), fromLocal, rvector ({2},{1}));//F->D(normal packet)
  staticRoutingF->AddHostRouteTo (Ipv4Address ("172.16.1.2"), iDF.GetAddress (1,0), rvector ({2},{1}));//F->D(TCP)
  staticRoutingF->AddHostRouteTo (Ipv4Address ("172.16.1.2"), ifInAddrF.GetLocal (), rvector ({2},{1}));//F->D(TCP)
  staticRoutingF->AddHostRouteTo (iAB.GetAddress (1,0), ifInAddrF.GetLocal (), rvector ({2},{1}));//F->D(TCP)
  staticRoutingF->AddHostRouteTo (iBC.GetAddress (0,0), ifInAddrF.GetLocal (), rvector ({2},{1}));//F->D(TCP)
  staticRoutingF->AddHostRouteTo (iBD.GetAddress (0,0), ifInAddrF.GetLocal (), rvector ({2},{1}));//F->D(TCP)
  staticRoutingD->AddHostRouteTo (Ipv4Address ("172.16.1.2"), iDF.GetAddress (1,0), rvector ({1},{1}));//D->B
  staticRoutingD->AddHostRouteTo (Ipv4Address ("172.16.1.2"), ifInAddrF.GetLocal (), rvector ({1},{1}));//D->B(TCP)
  staticRoutingD->AddHostRouteTo (iAB.GetAddress (1,0), ifInAddrF.GetLocal (), rvector ({1},{1}));//D->B(TCP)
  staticRoutingD->AddHostRouteTo (iBC.GetAddress (0,0), ifInAddrF.GetLocal (), rvector ({1},{1}));//D->B(TCP)
  staticRoutingD->AddHostRouteTo (iBD.GetAddress (0,0), ifInAddrF.GetLocal (), rvector ({1},{1}));//D->B(TCP)

  staticRoutingF->AddHostRouteTo (Ipv4Address ("172.16.1.3"), fromLocal, rvector ({1},{1}));//F->E(normal packet)
  staticRoutingF->AddHostRouteTo (Ipv4Address ("172.16.1.3"), iEF.GetAddress (1,0), rvector ({1},{1}));//F->E(TCP)
  staticRoutingF->AddHostRouteTo (Ipv4Address ("172.16.1.3"), ifInAddrF.GetLocal (), rvector ({1},{1}));//F->E(TCP)
  staticRoutingF->AddHostRouteTo (iAC.GetAddress (1,0), ifInAddrF.GetLocal (), rvector ({1},{1}));//F->E(TCP)
  staticRoutingF->AddHostRouteTo (iBC.GetAddress (1,0), ifInAddrF.GetLocal (), rvector ({1},{1}));//F->E(TCP)
  staticRoutingF->AddHostRouteTo (iCE.GetAddress (0,0), ifInAddrF.GetLocal (), rvector ({1},{1}));//F->E(TCP)
  staticRoutingE->AddHostRouteTo (Ipv4Address ("172.16.1.3"), iEF.GetAddress (1,0), rvector ({1},{1}));//E->C
  staticRoutingE->AddHostRouteTo (Ipv4Address ("172.16.1.3"), ifInAddrF.GetLocal (), rvector ({1},{1}));//E->C(TCP)
  staticRoutingE->AddHostRouteTo (iAC.GetAddress (1,0), ifInAddrF.GetLocal (), rvector ({1},{1}));//E->C(TCP)
  staticRoutingE->AddHostRouteTo (iBC.GetAddress (1,0), ifInAddrF.GetLocal (), rvector ({1},{1}));//E->C(TCP)
  staticRoutingE->AddHostRouteTo (iCE.GetAddress (0,0), ifInAddrF.GetLocal (), rvector ({1},{1}));//E->C(TCP)

  staticRoutingF->AddHostRouteTo (Ipv4Address ("172.16.1.4"), fromLocal, rvector ({2},{1}));//F->D(normal packet)
  staticRoutingF->AddHostRouteTo (Ipv4Address ("172.16.1.4"), iDF.GetAddress (1,0), rvector ({2},{1}));//F->D(TCP)
  staticRoutingF->AddHostRouteTo (Ipv4Address ("172.16.1.4"), ifInAddrF.GetLocal (), rvector ({2},{1}));//F->D(TCP)
  staticRoutingF->AddHostRouteTo (iBD.GetAddress (1,0), ifInAddrF.GetLocal (), rvector ({2},{1}));//F->D(TCP)
  staticRoutingF->AddHostRouteTo (iDF.GetAddress (0,0), ifInAddrF.GetLocal (), rvector ({2},{1}));//F->D(TCP)

  staticRoutingF->AddHostRouteTo (Ipv4Address ("172.16.1.5"), fromLocal, rvector ({1},{1}));//F->E(normal packet)
  staticRoutingF->AddHostRouteTo (Ipv4Address ("172.16.1.5"), iEF.GetAddress (1,0), rvector ({1},{1}));//F->E(TCP)
  staticRoutingF->AddHostRouteTo (Ipv4Address ("172.16.1.5"), ifInAddrF.GetLocal (), rvector ({1},{1}));//F->E(TCP)
  staticRoutingF->AddHostRouteTo (iCE.GetAddress (1,0), ifInAddrF.GetLocal (), rvector ({1},{1}));//F->E(TCP)
  staticRoutingF->AddHostRouteTo (iEF.GetAddress (0,0), ifInAddrF.GetLocal (), rvector ({1},{1}));//F->E(TCP)
  staticRoutingF->AddHostRouteTo (iEH.GetAddress (0,0), ifInAddrF.GetLocal (), rvector ({1},{1}));//F->E(TCP)

  staticRoutingF->AddHostRouteTo (Ipv4Address ("172.16.1.7"), fromLocal, rvector ({1},{1}));//F->E(normal packet)
  staticRoutingF->AddHostRouteTo (Ipv4Address ("172.16.1.7"), iEF.GetAddress (1,0), rvector ({1},{1}));//F->E(TCP)
  staticRoutingF->AddHostRouteTo (Ipv4Address ("172.16.1.7"), ifInAddrF.GetLocal (), rvector ({1},{1}));//F->E(TCP)
  staticRoutingF->AddHostRouteTo (iGH.GetAddress (0,0), ifInAddrF.GetLocal (), rvector ({1},{1}));//F->E(TCP)
  staticRoutingF->AddHostRouteTo (iGK.GetAddress (0,0), ifInAddrF.GetLocal (), rvector ({1},{1}));//F->E(TCP)
  staticRoutingE->AddHostRouteTo (Ipv4Address ("172.16.1.7"), iEF.GetAddress (1,0), rvector ({3},{1}));//E->H
  staticRoutingE->AddHostRouteTo (Ipv4Address ("172.16.1.7"), ifInAddrF.GetLocal (), rvector ({3},{1}));//E->H(TCP)
  staticRoutingE->AddHostRouteTo (iGH.GetAddress (0,0), ifInAddrF.GetLocal (), rvector ({3},{1}));//E->H(TCP)
  staticRoutingE->AddHostRouteTo (iGK.GetAddress (0,0), ifInAddrF.GetLocal (), rvector ({3},{1}));//E->H(TCP)
  staticRoutingH->AddHostRouteTo (Ipv4Address ("172.16.1.7"), iEF.GetAddress (1,0), rvector ({2},{1}));//H->G
  staticRoutingH->AddHostRouteTo (Ipv4Address ("172.16.1.7"), ifInAddrF.GetLocal (), rvector ({2},{1}));//H->G(TCP)
  staticRoutingH->AddHostRouteTo (iGH.GetAddress (0,0), ifInAddrF.GetLocal (), rvector ({2},{1}));//H->G(TCP)
  staticRoutingH->AddHostRouteTo (iGK.GetAddress (0,0), ifInAddrF.GetLocal (), rvector ({2},{1}));//H->G(TCP)

  staticRoutingF->AddHostRouteTo (Ipv4Address ("172.16.1.8"), fromLocal, rvector ({3},{1}));//F->I(normal packet)
  staticRoutingF->AddHostRouteTo (Ipv4Address ("172.16.1.8"), iFI.GetAddress (0,0), rvector ({3},{1}));//F->I(TCP)
  staticRoutingF->AddHostRouteTo (Ipv4Address ("172.16.1.8"), ifInAddrF.GetLocal (), rvector ({3},{1}));//F->I(TCP)
  staticRoutingF->AddHostRouteTo (iEH.GetAddress (1,0), ifInAddrF.GetLocal (), rvector ({3},{1}));//F->I(TCP)
  staticRoutingF->AddHostRouteTo (iGH.GetAddress (1,0), ifInAddrF.GetLocal (), rvector ({3},{1}));//F->I(TCP)
  staticRoutingF->AddHostRouteTo (iHI.GetAddress (0,0), ifInAddrF.GetLocal (), rvector ({3},{1}));//F->I(TCP)
  staticRoutingI->AddHostRouteTo (Ipv4Address ("172.16.1.8"), iFI.GetAddress (0,0), rvector ({2},{1}));//I->H
  staticRoutingI->AddHostRouteTo (Ipv4Address ("172.16.1.8"), ifInAddrF.GetLocal (), rvector ({2},{1}));//I->H(TCP)
  staticRoutingI->AddHostRouteTo (iEH.GetAddress (1,0), ifInAddrF.GetLocal (), rvector ({2},{1}));//I->H(TCP)
  staticRoutingI->AddHostRouteTo (iGH.GetAddress (1,0), ifInAddrF.GetLocal (), rvector ({2},{1}));//I->H(TCP)
  staticRoutingI->AddHostRouteTo (iHI.GetAddress (0,0), ifInAddrF.GetLocal (), rvector ({2},{1}));//I->H(TCP)

  staticRoutingF->AddHostRouteTo (Ipv4Address ("172.16.1.9"), fromLocal, rvector ({3},{1}));//F->I(normal packet)
  staticRoutingF->AddHostRouteTo (Ipv4Address ("172.16.1.9"), iFI.GetAddress (0,0), rvector ({3},{1}));//F->I(TCP)
  staticRoutingF->AddHostRouteTo (Ipv4Address ("172.16.1.9"), ifInAddrF.GetLocal (), rvector ({3},{1}));//F->I(TCP)
  staticRoutingF->AddHostRouteTo (iFI.GetAddress (1,0), ifInAddrF.GetLocal (), rvector ({3},{1}));//F->I(TCP)
  staticRoutingF->AddHostRouteTo (iHI.GetAddress (1,0), ifInAddrF.GetLocal (), rvector ({3},{1}));//F->I(TCP)
  staticRoutingF->AddHostRouteTo (iIJ.GetAddress (0,0), ifInAddrF.GetLocal (), rvector ({3},{1}));//F->I(TCP)

  staticRoutingF->AddHostRouteTo (Ipv4Address ("172.16.1.10"), fromLocal, rvector ({3},{1}));//F->I(normal packet)
  staticRoutingF->AddHostRouteTo (Ipv4Address ("172.16.1.10"), iFI.GetAddress (0,0), rvector ({3},{1}));//F->I(TCP)
  staticRoutingF->AddHostRouteTo (Ipv4Address ("172.16.1.10"), ifInAddrF.GetLocal (), rvector ({3},{1}));//F->I(TCP)
  staticRoutingF->AddHostRouteTo (iIJ.GetAddress (1,0), ifInAddrF.GetLocal (), rvector ({3},{1}));//F->I(TCP)
  staticRoutingF->AddHostRouteTo (iJK.GetAddress (0,0), ifInAddrF.GetLocal (), rvector ({3},{1}));//F->I(TCP)
  staticRoutingI->AddHostRouteTo (Ipv4Address ("172.16.1.10"), iFI.GetAddress (0,0), rvector ({3},{1}));//I->J
  staticRoutingI->AddHostRouteTo (Ipv4Address ("172.16.1.10"), ifInAddrF.GetLocal (), rvector ({3},{1}));//I->J(TCP)
  staticRoutingI->AddHostRouteTo (iIJ.GetAddress (1,0), ifInAddrF.GetLocal (), rvector ({3},{1}));//I->J(TCP)
  staticRoutingI->AddHostRouteTo (iJK.GetAddress (0,0), ifInAddrF.GetLocal (), rvector ({3},{1}));//I->J(TCP)

  staticRoutingF->AddHostRouteTo (Ipv4Address ("172.16.1.11"), fromLocal, rvector ({3},{1}));//F->I(normal packet)
  staticRoutingF->AddHostRouteTo (Ipv4Address ("172.16.1.11"), iFI.GetAddress (0,0), rvector ({3},{1}));//F->I(TCP)
  staticRoutingF->AddHostRouteTo (Ipv4Address ("172.16.1.11"), ifInAddrF.GetLocal (), rvector ({3},{1}));//F->I(TCP)
  staticRoutingF->AddHostRouteTo (iGK.GetAddress (1,0), ifInAddrF.GetLocal (), rvector ({3},{1}));//F->I(TCP)
  staticRoutingF->AddHostRouteTo (iJK.GetAddress (1,0), ifInAddrF.GetLocal (), rvector ({3},{1}));//F->I(TCP)
  staticRoutingI->AddHostRouteTo (Ipv4Address ("172.16.1.11"), iFI.GetAddress (0,0), rvector ({3},{1}));//I->J
  staticRoutingI->AddHostRouteTo (Ipv4Address ("172.16.1.11"), ifInAddrF.GetLocal (), rvector ({3},{1}));//I->J(TCP)
  staticRoutingI->AddHostRouteTo (iGK.GetAddress (1,0), ifInAddrF.GetLocal (), rvector ({3},{1}));//I->J(TCP)
  staticRoutingI->AddHostRouteTo (iJK.GetAddress (1,0), ifInAddrF.GetLocal (), rvector ({3},{1}));//I->J(TCP)
  staticRoutingJ->AddHostRouteTo (Ipv4Address ("172.16.1.11"), iFI.GetAddress (0,0), rvector ({2},{1}));//J->K
  staticRoutingJ->AddHostRouteTo (Ipv4Address ("172.16.1.11"), ifInAddrF.GetLocal (), rvector ({2},{1}));//J->K(TCP)
  staticRoutingJ->AddHostRouteTo (iGK.GetAddress (1,0), ifInAddrF.GetLocal (), rvector ({2},{1}));//J->K(TCP)
  staticRoutingJ->AddHostRouteTo (iJK.GetAddress (1,0), ifInAddrF.GetLocal (), rvector ({2},{1}));//J->K(TCP)

  staticRoutingG->AddHostRouteTo (Ipv4Address ("172.16.1.1"), fromLocal, rvector ({1},{1}));//G->H(normal packet)
  staticRoutingG->AddHostRouteTo (Ipv4Address ("172.16.1.1"), iGH.GetAddress (0,0), rvector ({1},{1}));//G->H(TCP)
  staticRoutingG->AddHostRouteTo (Ipv4Address ("172.16.1.1"), ifInAddrG.GetLocal (), rvector ({1},{1}));//G->H(TCP)
  staticRoutingG->AddHostRouteTo (iAB.GetAddress (0,0), ifInAddrG.GetLocal (), rvector ({1},{1}));//G->H(TCP)
  staticRoutingG->AddHostRouteTo (iAC.GetAddress (0,0), ifInAddrG.GetLocal (), rvector ({1},{1}));//G->H(TCP)
  staticRoutingH->AddHostRouteTo (Ipv4Address ("172.16.1.1"), iGH.GetAddress (0,0), rvector ({1},{1}));//H->E
  staticRoutingH->AddHostRouteTo (Ipv4Address ("172.16.1.1"), ifInAddrG.GetLocal (), rvector ({1},{1}));//H->E(TCP)
  staticRoutingH->AddHostRouteTo (iAB.GetAddress (0,0), ifInAddrG.GetLocal (), rvector ({1},{1}));//H->E(TCP)
  staticRoutingH->AddHostRouteTo (iAC.GetAddress (0,0), ifInAddrG.GetLocal (), rvector ({1},{1}));//H->E(TCP)
  staticRoutingE->AddHostRouteTo (Ipv4Address ("172.16.1.1"), iGH.GetAddress (0,0), rvector ({1},{1}));//E->C
  staticRoutingE->AddHostRouteTo (Ipv4Address ("172.16.1.1"), ifInAddrG.GetLocal (), rvector ({1},{1}));//E->C(TCP)
  staticRoutingE->AddHostRouteTo (iAB.GetAddress (0,0), ifInAddrG.GetLocal (), rvector ({1},{1}));//E->C(TCP)
  staticRoutingE->AddHostRouteTo (iAC.GetAddress (0,0), ifInAddrG.GetLocal (), rvector ({1},{1}));//E->C(TCP)
  staticRoutingC->AddHostRouteTo (Ipv4Address ("172.16.1.1"), iGH.GetAddress (0,0), rvector ({1},{1}));//C->A
  staticRoutingC->AddHostRouteTo (Ipv4Address ("172.16.1.1"), ifInAddrG.GetLocal (), rvector ({1},{1}));//C->A(TCP)
  staticRoutingC->AddHostRouteTo (iAB.GetAddress (0,0), ifInAddrG.GetLocal (), rvector ({1},{1}));//C->A(TCP)
  staticRoutingC->AddHostRouteTo (iAC.GetAddress (0,0), ifInAddrG.GetLocal (), rvector ({1},{1}));//C->A(TCP)

  staticRoutingG->AddHostRouteTo (Ipv4Address ("172.16.1.2"), fromLocal, rvector ({1},{1}));//G->H(normal packet)
  staticRoutingG->AddHostRouteTo (Ipv4Address ("172.16.1.2"), iGH.GetAddress (0,0), rvector ({1},{1}));//G->H(TCP)
  staticRoutingG->AddHostRouteTo (Ipv4Address ("172.16.1.2"), ifInAddrG.GetLocal (), rvector ({1},{1}));//G->H(TCP)
  staticRoutingG->AddHostRouteTo (iAB.GetAddress (1,0), ifInAddrG.GetLocal (), rvector ({1},{1}));//G->H(TCP)
  staticRoutingG->AddHostRouteTo (iBC.GetAddress (0,0), ifInAddrG.GetLocal (), rvector ({1},{1}));//G->H(TCP)
  staticRoutingG->AddHostRouteTo (iBD.GetAddress (0,0), ifInAddrG.GetLocal (), rvector ({1},{1}));//G->H(TCP)
  staticRoutingH->AddHostRouteTo (Ipv4Address ("172.16.1.2"), iGH.GetAddress (0,0), rvector ({1},{1}));//H->E
  staticRoutingH->AddHostRouteTo (Ipv4Address ("172.16.1.2"), ifInAddrG.GetLocal (), rvector ({1},{1}));//H->E(TCP)
  staticRoutingH->AddHostRouteTo (iAB.GetAddress (1,0), ifInAddrG.GetLocal (), rvector ({1},{1}));//H->E(TCP)
  staticRoutingH->AddHostRouteTo (iBC.GetAddress (0,0), ifInAddrG.GetLocal (), rvector ({1},{1}));//H->E(TCP)
  staticRoutingH->AddHostRouteTo (iBD.GetAddress (0,0), ifInAddrG.GetLocal (), rvector ({1},{1}));//H->E(TCP)
  staticRoutingE->AddHostRouteTo (Ipv4Address ("172.16.1.2"), iGH.GetAddress (0,0), rvector ({1},{1}));//E->C
  staticRoutingE->AddHostRouteTo (Ipv4Address ("172.16.1.2"), ifInAddrG.GetLocal (), rvector ({1},{1}));//E->C(TCP)
  staticRoutingE->AddHostRouteTo (iAB.GetAddress (1,0), ifInAddrG.GetLocal (), rvector ({1},{1}));//E->C(TCP)
  staticRoutingE->AddHostRouteTo (iBC.GetAddress (0,0), ifInAddrG.GetLocal (), rvector ({1},{1}));//E->C(TCP)
  staticRoutingE->AddHostRouteTo (iBD.GetAddress (0,0), ifInAddrG.GetLocal (), rvector ({1},{1}));//E->C(TCP)
  staticRoutingC->AddHostRouteTo (Ipv4Address ("172.16.1.2"), iGH.GetAddress (0,0), rvector ({2},{1}));//C->B
  staticRoutingC->AddHostRouteTo (Ipv4Address ("172.16.1.2"), ifInAddrG.GetLocal (), rvector ({2},{1}));//C->B(TCP)
  staticRoutingC->AddHostRouteTo (iAB.GetAddress (1,0), ifInAddrG.GetLocal (), rvector ({2},{1}));//C->B(TCP)
  staticRoutingC->AddHostRouteTo (iBC.GetAddress (0,0), ifInAddrG.GetLocal (), rvector ({2},{1}));//C->B(TCP)
  staticRoutingC->AddHostRouteTo (iBD.GetAddress (0,0), ifInAddrG.GetLocal (), rvector ({2},{1}));//C->B(TCP)

  staticRoutingG->AddHostRouteTo (Ipv4Address ("172.16.1.3"), fromLocal, rvector ({1},{1}));//G->H(normal packet)
  staticRoutingG->AddHostRouteTo (Ipv4Address ("172.16.1.3"), iGH.GetAddress (0,0), rvector ({1},{1}));//G->H(TCP)
  staticRoutingG->AddHostRouteTo (Ipv4Address ("172.16.1.3"), ifInAddrG.GetLocal (), rvector ({1},{1}));//G->H(TCP)
  staticRoutingG->AddHostRouteTo (iAC.GetAddress (1,0), ifInAddrG.GetLocal (), rvector ({1},{1}));//G->H(TCP)
  staticRoutingG->AddHostRouteTo (iBC.GetAddress (1,0), ifInAddrG.GetLocal (), rvector ({1},{1}));//G->H(TCP)
  staticRoutingG->AddHostRouteTo (iCE.GetAddress (0,0), ifInAddrG.GetLocal (), rvector ({1},{1}));//G->H(TCP)
  staticRoutingH->AddHostRouteTo (Ipv4Address ("172.16.1.3"), iGH.GetAddress (0,0), rvector ({1},{1}));//H->E
  staticRoutingH->AddHostRouteTo (Ipv4Address ("172.16.1.3"), ifInAddrG.GetLocal (), rvector ({1},{1}));//H->E(TCP)
  staticRoutingH->AddHostRouteTo (iAC.GetAddress (1,0), ifInAddrG.GetLocal (), rvector ({1},{1}));//H->E(TCP)
  staticRoutingH->AddHostRouteTo (iBC.GetAddress (1,0), ifInAddrG.GetLocal (), rvector ({1},{1}));//H->E(TCP)
  staticRoutingH->AddHostRouteTo (iCE.GetAddress (0,0), ifInAddrG.GetLocal (), rvector ({1},{1}));//H->E(TCP)
  staticRoutingE->AddHostRouteTo (Ipv4Address ("172.16.1.3"), iGH.GetAddress (0,0), rvector ({1},{1}));//E->C
  staticRoutingE->AddHostRouteTo (Ipv4Address ("172.16.1.3"), ifInAddrG.GetLocal (), rvector ({1},{1}));//E->C(TCP)
  staticRoutingE->AddHostRouteTo (iAC.GetAddress (1,0), ifInAddrG.GetLocal (), rvector ({1},{1}));//E->C(TCP)
  staticRoutingE->AddHostRouteTo (iBC.GetAddress (1,0), ifInAddrG.GetLocal (), rvector ({1},{1}));//E->C(TCP)
  staticRoutingE->AddHostRouteTo (iCE.GetAddress (0,0), ifInAddrG.GetLocal (), rvector ({1},{1}));//E->C(TCP)

  staticRoutingG->AddHostRouteTo (Ipv4Address ("172.16.1.4"), fromLocal, rvector ({1},{1}));//G->H(normal packet)
  staticRoutingG->AddHostRouteTo (Ipv4Address ("172.16.1.4"), iGH.GetAddress (0,0), rvector ({1},{1}));//G->H(TCP)
  staticRoutingG->AddHostRouteTo (Ipv4Address ("172.16.1.4"), ifInAddrG.GetLocal (), rvector ({1},{1}));//G->H(TCP)
  staticRoutingG->AddHostRouteTo (iBD.GetAddress (1,0), ifInAddrG.GetLocal (), rvector ({1},{1}));//G->H(TCP)
  staticRoutingG->AddHostRouteTo (iDF.GetAddress (0,0), ifInAddrG.GetLocal (), rvector ({1},{1}));//G->H(TCP)
  staticRoutingH->AddHostRouteTo (Ipv4Address ("172.16.1.4"), iGH.GetAddress (0,0), rvector ({3},{1}));//H->I
  staticRoutingH->AddHostRouteTo (Ipv4Address ("172.16.1.4"), ifInAddrG.GetLocal (), rvector ({3},{1}));//H->I(TCP)
  staticRoutingH->AddHostRouteTo (iBD.GetAddress (1,0), ifInAddrG.GetLocal (), rvector ({3},{1}));//H->I(TCP)
  staticRoutingH->AddHostRouteTo (iDF.GetAddress (0,0), ifInAddrG.GetLocal (), rvector ({3},{1}));//H->I(TCP)
  staticRoutingI->AddHostRouteTo (Ipv4Address ("172.16.1.4"), iGH.GetAddress (0,0), rvector ({1},{1}));//I->F
  staticRoutingI->AddHostRouteTo (Ipv4Address ("172.16.1.4"), ifInAddrG.GetLocal (), rvector ({1},{1}));//I->F(TCP)
  staticRoutingI->AddHostRouteTo (iBD.GetAddress (1,0), ifInAddrG.GetLocal (), rvector ({1},{1}));//I->F(TCP)
  staticRoutingI->AddHostRouteTo (iDF.GetAddress (0,0), ifInAddrG.GetLocal (), rvector ({1},{1}));//I->F(TCP)
  staticRoutingF->AddHostRouteTo (Ipv4Address ("172.16.1.4"), iGH.GetAddress (0,0), rvector ({2},{1}));//F->D
  staticRoutingF->AddHostRouteTo (Ipv4Address ("172.16.1.4"), ifInAddrG.GetLocal (), rvector ({2},{1}));//F->D(TCP)
  staticRoutingF->AddHostRouteTo (iBD.GetAddress (1,0), ifInAddrG.GetLocal (), rvector ({2},{1}));//F->D(TCP)
  staticRoutingF->AddHostRouteTo (iDF.GetAddress (0,0), ifInAddrG.GetLocal (), rvector ({2},{1}));//F->D(TCP)

  staticRoutingG->AddHostRouteTo (Ipv4Address ("172.16.1.5"), fromLocal, rvector ({1},{1}));//G->H(normal packet)
  staticRoutingG->AddHostRouteTo (Ipv4Address ("172.16.1.5"), iGH.GetAddress (0,0), rvector ({1},{1}));//G->H(TCP)
  staticRoutingG->AddHostRouteTo (Ipv4Address ("172.16.1.5"), ifInAddrG.GetLocal (), rvector ({1},{1}));//G->H(TCP)
  staticRoutingG->AddHostRouteTo (iCE.GetAddress (1,0), ifInAddrG.GetLocal (), rvector ({1},{1}));//G->H(TCP)
  staticRoutingG->AddHostRouteTo (iEF.GetAddress (0,0), ifInAddrG.GetLocal (), rvector ({1},{1}));//G->H(TCP)
  staticRoutingG->AddHostRouteTo (iEH.GetAddress (0,0), ifInAddrG.GetLocal (), rvector ({1},{1}));//G->H(TCP)
  staticRoutingH->AddHostRouteTo (Ipv4Address ("172.16.1.5"), iGH.GetAddress (0,0), rvector ({1},{1}));//H->E
  staticRoutingH->AddHostRouteTo (Ipv4Address ("172.16.1.5"), ifInAddrG.GetLocal (), rvector ({1},{1}));//H->E(TCP)
  staticRoutingH->AddHostRouteTo (iCE.GetAddress (1,0), ifInAddrG.GetLocal (), rvector ({1},{1}));//H->E(TCP)
  staticRoutingH->AddHostRouteTo (iEF.GetAddress (0,0), ifInAddrG.GetLocal (), rvector ({1},{1}));//H->E(TCP)
  staticRoutingH->AddHostRouteTo (iEH.GetAddress (0,0), ifInAddrG.GetLocal (), rvector ({1},{1}));//H->E(TCP)

  staticRoutingG->AddHostRouteTo (Ipv4Address ("172.16.1.8"), fromLocal, rvector ({1},{1}));//G->H(normal packet)
  staticRoutingG->AddHostRouteTo (Ipv4Address ("172.16.1.8"), iGH.GetAddress (0,0), rvector ({1},{1}));//G->H(TCP)
  staticRoutingG->AddHostRouteTo (Ipv4Address ("172.16.1.8"), ifInAddrG.GetLocal (), rvector ({1},{1}));//G->H(TCP)
  staticRoutingG->AddHostRouteTo (iEH.GetAddress (1,0), ifInAddrG.GetLocal (), rvector ({1},{1}));//G->H(TCP)
  staticRoutingG->AddHostRouteTo (iGH.GetAddress (1,0), ifInAddrG.GetLocal (), rvector ({1},{1}));//G->H(TCP)
  staticRoutingG->AddHostRouteTo (iHI.GetAddress (0,0), ifInAddrG.GetLocal (), rvector ({1},{1}));//G->H(TCP)

  staticRoutingG->AddHostRouteTo (Ipv4Address ("172.16.1.9"), fromLocal, rvector ({1},{1}));//G->H(normal packet)
  staticRoutingG->AddHostRouteTo (Ipv4Address ("172.16.1.9"), iGH.GetAddress (0,0), rvector ({1},{1}));//G->H(TCP)
  staticRoutingG->AddHostRouteTo (Ipv4Address ("172.16.1.9"), ifInAddrG.GetLocal (), rvector ({1},{1}));//G->H(TCP)
  staticRoutingG->AddHostRouteTo (iFI.GetAddress (1,0), ifInAddrG.GetLocal (), rvector ({1},{1}));//G->H(TCP)
  staticRoutingG->AddHostRouteTo (iHI.GetAddress (1,0), ifInAddrG.GetLocal (), rvector ({1},{1}));//G->H(TCP)
  staticRoutingG->AddHostRouteTo (iIJ.GetAddress (0,0), ifInAddrG.GetLocal (), rvector ({1},{1}));//G->H(TCP)
  staticRoutingH->AddHostRouteTo (Ipv4Address ("172.16.1.9"), iGH.GetAddress (0,0), rvector ({3},{1}));//H->I
  staticRoutingH->AddHostRouteTo (Ipv4Address ("172.16.1.9"), ifInAddrG.GetLocal (), rvector ({3},{1}));//H->I(TCP)
  staticRoutingH->AddHostRouteTo (iFI.GetAddress (1,0), ifInAddrG.GetLocal (), rvector ({3},{1}));//H->I(TCP)
  staticRoutingH->AddHostRouteTo (iHI.GetAddress (1,0), ifInAddrG.GetLocal (), rvector ({3},{1}));//H->I(TCP)
  staticRoutingH->AddHostRouteTo (iIJ.GetAddress (0,0), ifInAddrG.GetLocal (), rvector ({3},{1}));//H->I(TCP)

  staticRoutingG->AddHostRouteTo (Ipv4Address ("172.16.1.10"), fromLocal, rvector ({2},{1}));//G->K(normal packet)
  staticRoutingG->AddHostRouteTo (Ipv4Address ("172.16.1.10"), iGK.GetAddress (0,0), rvector ({2},{1}));//G->K(TCP)
  staticRoutingG->AddHostRouteTo (Ipv4Address ("172.16.1.10"), ifInAddrG.GetLocal (), rvector ({2},{1}));//G->K(TCP)
  staticRoutingG->AddHostRouteTo (iIJ.GetAddress (1,0), ifInAddrG.GetLocal (), rvector ({2},{1}));//G->K(TCP)
  staticRoutingG->AddHostRouteTo (iJK.GetAddress (0,0), ifInAddrG.GetLocal (), rvector ({2},{1}));//G->K(TCP)
  staticRoutingK->AddHostRouteTo (Ipv4Address ("172.16.1.10"), iGK.GetAddress (0,0), rvector ({2},{1}));//K->J
  staticRoutingK->AddHostRouteTo (Ipv4Address ("172.16.1.10"), ifInAddrG.GetLocal (), rvector ({2},{1}));//K->J(TCP)
  staticRoutingK->AddHostRouteTo (iIJ.GetAddress (1,0), ifInAddrG.GetLocal (), rvector ({2},{1}));//K->J(TCP)
  staticRoutingK->AddHostRouteTo (iJK.GetAddress (0,0), ifInAddrG.GetLocal (), rvector ({2},{1}));//K->J(TCP)

  staticRoutingG->AddHostRouteTo (Ipv4Address ("172.16.1.11"), fromLocal, rvector ({2},{1}));//G->K(normal packet)
  staticRoutingG->AddHostRouteTo (Ipv4Address ("172.16.1.11"), iGK.GetAddress (0,0), rvector ({2},{1}));//G->K(TCP)
  staticRoutingG->AddHostRouteTo (Ipv4Address ("172.16.1.11"), ifInAddrG.GetLocal (), rvector ({2},{1}));//G->K(TCP)
  staticRoutingG->AddHostRouteTo (iGK.GetAddress (1,0), ifInAddrG.GetLocal (), rvector ({2},{1}));//G->K(TCP)
  staticRoutingG->AddHostRouteTo (iJK.GetAddress (1,0), ifInAddrG.GetLocal (), rvector ({2},{1}));//G->K(TCP)

  staticRoutingH->AddHostRouteTo (Ipv4Address ("172.16.1.1"), fromLocal, rvector ({1},{1}));//H->E(normal packet)
  staticRoutingH->AddHostRouteTo (Ipv4Address ("172.16.1.1"), iEH.GetAddress (1,0), rvector ({1},{1}));//H->E(TCP)
  staticRoutingH->AddHostRouteTo (Ipv4Address ("172.16.1.1"), ifInAddrH.GetLocal (), rvector ({1},{1}));//H->E(TCP)
  staticRoutingH->AddHostRouteTo (iAB.GetAddress (0,0), ifInAddrH.GetLocal (), rvector ({1},{1}));//H->E(TCP)
  staticRoutingH->AddHostRouteTo (iAC.GetAddress (0,0), ifInAddrH.GetLocal (), rvector ({1},{1}));//H->E(TCP)
  staticRoutingE->AddHostRouteTo (Ipv4Address ("172.16.1.1"), iEH.GetAddress (1,0), rvector ({1},{1}));//E->C
  staticRoutingE->AddHostRouteTo (Ipv4Address ("172.16.1.1"), ifInAddrH.GetLocal (), rvector ({1},{1}));//E->C(TCP)
  staticRoutingE->AddHostRouteTo (iAB.GetAddress (0,0), ifInAddrH.GetLocal (), rvector ({1},{1}));//E->C(TCP)
  staticRoutingE->AddHostRouteTo (iAC.GetAddress (0,0), ifInAddrH.GetLocal (), rvector ({1},{1}));//E->C(TCP)
  staticRoutingC->AddHostRouteTo (Ipv4Address ("172.16.1.1"), iEH.GetAddress (1,0), rvector ({1},{1}));//C->A
  staticRoutingC->AddHostRouteTo (Ipv4Address ("172.16.1.1"), ifInAddrH.GetLocal (), rvector ({1},{1}));//C->A(TCP)
  staticRoutingC->AddHostRouteTo (iAB.GetAddress (0,0), ifInAddrH.GetLocal (), rvector ({1},{1}));//C->A(TCP)
  staticRoutingC->AddHostRouteTo (iAC.GetAddress (0,0), ifInAddrH.GetLocal (), rvector ({1},{1}));//C->A(TCP)

  staticRoutingH->AddHostRouteTo (Ipv4Address ("172.16.1.2"), fromLocal, rvector ({1},{1}));//H->E(normal packet)
  staticRoutingH->AddHostRouteTo (Ipv4Address ("172.16.1.2"), iEH.GetAddress (1,0), rvector ({1},{1}));//H->E(TCP)
  staticRoutingH->AddHostRouteTo (Ipv4Address ("172.16.1.2"), ifInAddrH.GetLocal (), rvector ({1},{1}));//H->E(TCP)
  staticRoutingH->AddHostRouteTo (iAB.GetAddress (1,0), ifInAddrH.GetLocal (), rvector ({1},{1}));//H->E(TCP)
  staticRoutingH->AddHostRouteTo (iBC.GetAddress (0,0), ifInAddrH.GetLocal (), rvector ({1},{1}));//H->E(TCP)
  staticRoutingH->AddHostRouteTo (iBD.GetAddress (0,0), ifInAddrH.GetLocal (), rvector ({1},{1}));//H->E(TCP)
  staticRoutingE->AddHostRouteTo (Ipv4Address ("172.16.1.2"), iEH.GetAddress (1,0), rvector ({1},{1}));//E->C
  staticRoutingE->AddHostRouteTo (Ipv4Address ("172.16.1.2"), ifInAddrH.GetLocal (), rvector ({1},{1}));//E->C(TCP)
  staticRoutingE->AddHostRouteTo (iAB.GetAddress (1,0), ifInAddrH.GetLocal (), rvector ({1},{1}));//E->C(TCP)
  staticRoutingE->AddHostRouteTo (iBC.GetAddress (0,0), ifInAddrH.GetLocal (), rvector ({1},{1}));//E->C(TCP)
  staticRoutingE->AddHostRouteTo (iBD.GetAddress (0,0), ifInAddrH.GetLocal (), rvector ({1},{1}));//E->C(TCP)
  staticRoutingC->AddHostRouteTo (Ipv4Address ("172.16.1.2"), iEH.GetAddress (1,0), rvector ({2},{1}));//C->B
  staticRoutingC->AddHostRouteTo (Ipv4Address ("172.16.1.2"), ifInAddrH.GetLocal (), rvector ({2},{1}));//C->B(TCP)
  staticRoutingC->AddHostRouteTo (iAB.GetAddress (1,0), ifInAddrH.GetLocal (), rvector ({2},{1}));//C->B(TCP)
  staticRoutingC->AddHostRouteTo (iBC.GetAddress (0,0), ifInAddrH.GetLocal (), rvector ({2},{1}));//C->B(TCP)
  staticRoutingC->AddHostRouteTo (iBD.GetAddress (0,0), ifInAddrH.GetLocal (), rvector ({2},{1}));//C->B(TCP)

  staticRoutingH->AddHostRouteTo (Ipv4Address ("172.16.1.3"), fromLocal, rvector ({1},{1}));//H->E(normal packet)
  staticRoutingH->AddHostRouteTo (Ipv4Address ("172.16.1.3"), iEH.GetAddress (1,0), rvector ({1},{1}));//H->E(TCP)
  staticRoutingH->AddHostRouteTo (Ipv4Address ("172.16.1.3"), ifInAddrH.GetLocal (), rvector ({1},{1}));//H->E(TCP)
  staticRoutingH->AddHostRouteTo (iAC.GetAddress (1,0), ifInAddrH.GetLocal (), rvector ({1},{1}));//H->E(TCP)
  staticRoutingH->AddHostRouteTo (iBC.GetAddress (1,0), ifInAddrH.GetLocal (), rvector ({1},{1}));//H->E(TCP)
  staticRoutingH->AddHostRouteTo (iCE.GetAddress (0,0), ifInAddrH.GetLocal (), rvector ({1},{1}));//H->E(TCP)
  staticRoutingE->AddHostRouteTo (Ipv4Address ("172.16.1.3"), iEH.GetAddress (1,0), rvector ({1},{1}));//E->C
  staticRoutingE->AddHostRouteTo (Ipv4Address ("172.16.1.3"), ifInAddrH.GetLocal (), rvector ({1},{1}));//E->C(TCP)
  staticRoutingE->AddHostRouteTo (iAC.GetAddress (1,0), ifInAddrH.GetLocal (), rvector ({1},{1}));//E->C(TCP)
  staticRoutingE->AddHostRouteTo (iBC.GetAddress (1,0), ifInAddrH.GetLocal (), rvector ({1},{1}));//E->C(TCP)
  staticRoutingE->AddHostRouteTo (iCE.GetAddress (0,0), ifInAddrH.GetLocal (), rvector ({1},{1}));//E->C(TCP)

  staticRoutingH->AddHostRouteTo (Ipv4Address ("172.16.1.5"), fromLocal, rvector ({1},{1}));//H->E(normal packet)
  staticRoutingH->AddHostRouteTo (Ipv4Address ("172.16.1.5"), iEH.GetAddress (1,0), rvector ({1},{1}));//H->E(TCP)
  staticRoutingH->AddHostRouteTo (Ipv4Address ("172.16.1.5"), ifInAddrH.GetLocal (), rvector ({1},{1}));//H->E(TCP)
  staticRoutingH->AddHostRouteTo (iCE.GetAddress (1,0), ifInAddrH.GetLocal (), rvector ({1},{1}));//H->E(TCP)
  staticRoutingH->AddHostRouteTo (iEF.GetAddress (0,0), ifInAddrH.GetLocal (), rvector ({1},{1}));//H->E(TCP)
  staticRoutingH->AddHostRouteTo (iEH.GetAddress (0,0), ifInAddrH.GetLocal (), rvector ({1},{1}));//H->E(TCP)

  staticRoutingH->AddHostRouteTo (Ipv4Address ("172.16.1.6"), fromLocal, rvector ({1},{1}));//H->E(normal packet)
  staticRoutingH->AddHostRouteTo (Ipv4Address ("172.16.1.6"), iEH.GetAddress (1,0), rvector ({1},{1}));//H->E(TCP)
  staticRoutingH->AddHostRouteTo (Ipv4Address ("172.16.1.6"), ifInAddrH.GetLocal (), rvector ({1},{1}));//H->E(TCP)
  staticRoutingH->AddHostRouteTo (iEF.GetAddress (1,0), ifInAddrH.GetLocal (), rvector ({1},{1}));//H->E(TCP)
  staticRoutingH->AddHostRouteTo (iDF.GetAddress (1,0), ifInAddrH.GetLocal (), rvector ({1},{1}));//H->E(TCP)
  staticRoutingH->AddHostRouteTo (iFI.GetAddress (0,0), ifInAddrH.GetLocal (), rvector ({1},{1}));//H->E(TCP)
  staticRoutingE->AddHostRouteTo (Ipv4Address ("172.16.1.6"), iEH.GetAddress (1,0), rvector ({2},{1}));//E->F
  staticRoutingE->AddHostRouteTo (Ipv4Address ("172.16.1.6"), ifInAddrH.GetLocal (), rvector ({2},{1}));//E->F(TCP)
  staticRoutingE->AddHostRouteTo (iEF.GetAddress (1,0), ifInAddrH.GetLocal (), rvector ({2},{1}));//E->F(TCP)
  staticRoutingE->AddHostRouteTo (iDF.GetAddress (1,0), ifInAddrH.GetLocal (), rvector ({2},{1}));//E->F(TCP)
  staticRoutingE->AddHostRouteTo (iFI.GetAddress (0,0), ifInAddrH.GetLocal (), rvector ({2},{1}));//E->F(TCP)

  staticRoutingH->AddHostRouteTo (Ipv4Address ("172.16.1.7"), fromLocal, rvector ({2},{1}));//H->G(normal packet)
  staticRoutingH->AddHostRouteTo (Ipv4Address ("172.16.1.7"), iGH.GetAddress (1,0), rvector ({2},{1}));//H->G(TCP)
  staticRoutingH->AddHostRouteTo (Ipv4Address ("172.16.1.7"), ifInAddrH.GetLocal (), rvector ({2},{1}));//H->G(TCP)
  staticRoutingH->AddHostRouteTo (iGH.GetAddress (0,0), ifInAddrH.GetLocal (), rvector ({2},{1}));//H->G(TCP)
  staticRoutingH->AddHostRouteTo (iGK.GetAddress (0,0), ifInAddrH.GetLocal (), rvector ({2},{1}));//H->G(TCP)

  staticRoutingH->AddHostRouteTo (Ipv4Address ("172.16.1.9"), fromLocal, rvector ({3},{1}));//H->I(normal packet)
  staticRoutingH->AddHostRouteTo (Ipv4Address ("172.16.1.9"), iHI.GetAddress (0,0), rvector ({3},{1}));//H->I(TCP)
  staticRoutingH->AddHostRouteTo (Ipv4Address ("172.16.1.9"), ifInAddrH.GetLocal (), rvector ({3},{1}));//H->I(TCP)
  staticRoutingH->AddHostRouteTo (iFI.GetAddress (1,0), ifInAddrH.GetLocal (), rvector ({3},{1}));//H->I(TCP)
  staticRoutingH->AddHostRouteTo (iHI.GetAddress (1,0), ifInAddrH.GetLocal (), rvector ({3},{1}));//H->I(TCP)
  staticRoutingH->AddHostRouteTo (iIJ.GetAddress (0,0), ifInAddrH.GetLocal (), rvector ({3},{1}));//H->I(TCP)

  staticRoutingH->AddHostRouteTo (Ipv4Address ("172.16.1.10"), fromLocal, rvector ({3},{1}));//H->I(normal packet)
  staticRoutingH->AddHostRouteTo (Ipv4Address ("172.16.1.10"), iHI.GetAddress (0,0), rvector ({3},{1}));//H->I(TCP)
  staticRoutingH->AddHostRouteTo (Ipv4Address ("172.16.1.10"), ifInAddrH.GetLocal (), rvector ({3},{1}));//H->I(TCP)
  staticRoutingH->AddHostRouteTo (iIJ.GetAddress (1,0), ifInAddrH.GetLocal (), rvector ({3},{1}));//H->I(TCP)
  staticRoutingH->AddHostRouteTo (iJK.GetAddress (0,0), ifInAddrH.GetLocal (), rvector ({3},{1}));//H->I(TCP)
  staticRoutingI->AddHostRouteTo (Ipv4Address ("172.16.1.10"), iHI.GetAddress (0,0), rvector ({3},{1}));//I->J
  staticRoutingI->AddHostRouteTo (Ipv4Address ("172.16.1.10"), ifInAddrH.GetLocal (), rvector ({3},{1}));//I->J(TCP)
  staticRoutingI->AddHostRouteTo (iIJ.GetAddress (1,0), ifInAddrH.GetLocal (), rvector ({3},{1}));//I->J(TCP)
  staticRoutingI->AddHostRouteTo (iJK.GetAddress (0,0), ifInAddrH.GetLocal (), rvector ({3},{1}));//I->J(TCP)

  staticRoutingH->AddHostRouteTo (Ipv4Address ("172.16.1.11"), fromLocal, rvector ({2},{1}));//H->G(normal packet)
  staticRoutingH->AddHostRouteTo (Ipv4Address ("172.16.1.11"), iGH.GetAddress (1,0), rvector ({2},{1}));//H->G(TCP)
  staticRoutingH->AddHostRouteTo (Ipv4Address ("172.16.1.11"), ifInAddrH.GetLocal (), rvector ({2},{1}));//H->G(TCP)
  staticRoutingH->AddHostRouteTo (iGK.GetAddress (1,0), ifInAddrH.GetLocal (), rvector ({2},{1}));//H->G(TCP)
  staticRoutingH->AddHostRouteTo (iJK.GetAddress (1,0), ifInAddrH.GetLocal (), rvector ({2},{1}));//H->G(TCP)
  staticRoutingG->AddHostRouteTo (Ipv4Address ("172.16.1.11"), iGH.GetAddress (1,0), rvector ({2},{1}));//G->K
  staticRoutingG->AddHostRouteTo (Ipv4Address ("172.16.1.11"), ifInAddrH.GetLocal (), rvector ({2},{1}));//G->K(TCP)
  staticRoutingG->AddHostRouteTo (iGK.GetAddress (1,0), ifInAddrH.GetLocal (), rvector ({2},{1}));//G->K(TCP)
  staticRoutingG->AddHostRouteTo (iJK.GetAddress (1,0), ifInAddrH.GetLocal (), rvector ({2},{1}));//G->K(TCP)

  staticRoutingI->AddHostRouteTo (Ipv4Address ("172.16.1.1"), fromLocal, rvector ({2},{1}));//I->H(normal packet)
  staticRoutingI->AddHostRouteTo (Ipv4Address ("172.16.1.1"), iHI.GetAddress (1,0), rvector ({2},{1}));//I->H(TCP)
  staticRoutingI->AddHostRouteTo (Ipv4Address ("172.16.1.1"), ifInAddrI.GetLocal (), rvector ({2},{1}));//I->H(TCP)
  staticRoutingI->AddHostRouteTo (iAB.GetAddress (0,0), ifInAddrI.GetLocal (), rvector ({2},{1}));//I->H(TCP)
  staticRoutingI->AddHostRouteTo (iAC.GetAddress (0,0), ifInAddrI.GetLocal (), rvector ({2},{1}));//I->H(TCP)
  staticRoutingH->AddHostRouteTo (Ipv4Address ("172.16.1.1"), iHI.GetAddress (1,0), rvector ({1},{1}));//H->E
  staticRoutingH->AddHostRouteTo (Ipv4Address ("172.16.1.1"), ifInAddrI.GetLocal (), rvector ({1},{1}));//H->E(TCP)
  staticRoutingH->AddHostRouteTo (iAB.GetAddress (0,0), ifInAddrI.GetLocal (), rvector ({1},{1}));//H->E(TCP)
  staticRoutingH->AddHostRouteTo (iAC.GetAddress (0,0), ifInAddrI.GetLocal (), rvector ({1},{1}));//H->E(TCP)
  staticRoutingE->AddHostRouteTo (Ipv4Address ("172.16.1.1"), iHI.GetAddress (1,0), rvector ({1},{1}));//E->C
  staticRoutingE->AddHostRouteTo (Ipv4Address ("172.16.1.1"), ifInAddrI.GetLocal (), rvector ({1},{1}));//E->C(TCP)
  staticRoutingE->AddHostRouteTo (iAB.GetAddress (0,0), ifInAddrI.GetLocal (), rvector ({1},{1}));//E->C(TCP)
  staticRoutingE->AddHostRouteTo (iAC.GetAddress (0,0), ifInAddrI.GetLocal (), rvector ({1},{1}));//E->C(TCP)
  staticRoutingC->AddHostRouteTo (Ipv4Address ("172.16.1.1"), iHI.GetAddress (1,0), rvector ({1},{1}));//C->A
  staticRoutingC->AddHostRouteTo (Ipv4Address ("172.16.1.1"), ifInAddrI.GetLocal (), rvector ({1},{1}));//C->A(TCP)
  staticRoutingC->AddHostRouteTo (iAB.GetAddress (0,0), ifInAddrI.GetLocal (), rvector ({1},{1}));//C->A(TCP)
  staticRoutingC->AddHostRouteTo (iAC.GetAddress (0,0), ifInAddrI.GetLocal (), rvector ({1},{1}));//C->A(TCP)

  staticRoutingI->AddHostRouteTo (Ipv4Address ("172.16.1.2"), fromLocal, rvector ({1},{1}));//I->F(normal packet)
  staticRoutingI->AddHostRouteTo (Ipv4Address ("172.16.1.2"), iFI.GetAddress (1,0), rvector ({1},{1}));//I->F(TCP)
  staticRoutingI->AddHostRouteTo (Ipv4Address ("172.16.1.2"), ifInAddrI.GetLocal (), rvector ({1},{1}));//I->F(TCP)
  staticRoutingI->AddHostRouteTo (iAB.GetAddress (1,0), ifInAddrI.GetLocal (), rvector ({1},{1}));//I->F(TCP)
  staticRoutingI->AddHostRouteTo (iBC.GetAddress (0,0), ifInAddrI.GetLocal (), rvector ({1},{1}));//I->F(TCP)
  staticRoutingI->AddHostRouteTo (iBD.GetAddress (0,0), ifInAddrI.GetLocal (), rvector ({1},{1}));//I->F(TCP)
  staticRoutingF->AddHostRouteTo (Ipv4Address ("172.16.1.2"), iFI.GetAddress (1,0), rvector ({2},{1}));//F->D
  staticRoutingF->AddHostRouteTo (Ipv4Address ("172.16.1.2"), ifInAddrI.GetLocal (), rvector ({2},{1}));//F->D(TCP)
  staticRoutingF->AddHostRouteTo (iAB.GetAddress (1,0), ifInAddrI.GetLocal (), rvector ({2},{1}));//F->D(TCP)
  staticRoutingF->AddHostRouteTo (iBC.GetAddress (0,0), ifInAddrI.GetLocal (), rvector ({2},{1}));//F->D(TCP)
  staticRoutingF->AddHostRouteTo (iBD.GetAddress (0,0), ifInAddrI.GetLocal (), rvector ({2},{1}));//F->D(TCP)
  staticRoutingD->AddHostRouteTo (Ipv4Address ("172.16.1.2"), iFI.GetAddress (1,0), rvector ({1},{1}));//D->B
  staticRoutingD->AddHostRouteTo (Ipv4Address ("172.16.1.2"), ifInAddrI.GetLocal (), rvector ({1},{1}));//D->B(TCP)
  staticRoutingD->AddHostRouteTo (iAB.GetAddress (1,0), ifInAddrI.GetLocal (), rvector ({1},{1}));//D->B(TCP)
  staticRoutingD->AddHostRouteTo (iBC.GetAddress (0,0), ifInAddrI.GetLocal (), rvector ({1},{1}));//D->B(TCP)
  staticRoutingD->AddHostRouteTo (iBD.GetAddress (0,0), ifInAddrI.GetLocal (), rvector ({1},{1}));//D->B(TCP)

  staticRoutingI->AddHostRouteTo (Ipv4Address ("172.16.1.3"), fromLocal, rvector ({1},{1}));//I->F(normal packet)
  staticRoutingI->AddHostRouteTo (Ipv4Address ("172.16.1.3"), iFI.GetAddress (1,0), rvector ({1},{1}));//I->F(TCP)
  staticRoutingI->AddHostRouteTo (Ipv4Address ("172.16.1.3"), ifInAddrI.GetLocal (), rvector ({1},{1}));//I->F(TCP)
  staticRoutingI->AddHostRouteTo (iAC.GetAddress (1,0), ifInAddrI.GetLocal (), rvector ({1},{1}));//I->F(TCP)
  staticRoutingI->AddHostRouteTo (iBC.GetAddress (1,0), ifInAddrI.GetLocal (), rvector ({1},{1}));//I->F(TCP)
  staticRoutingI->AddHostRouteTo (iCE.GetAddress (0,0), ifInAddrI.GetLocal (), rvector ({1},{1}));//I->F(TCP)
  staticRoutingF->AddHostRouteTo (Ipv4Address ("172.16.1.3"), iFI.GetAddress (1,0), rvector ({1},{1}));//F->E
  staticRoutingF->AddHostRouteTo (Ipv4Address ("172.16.1.3"), ifInAddrI.GetLocal (), rvector ({1},{1}));//F->E(TCP)
  staticRoutingF->AddHostRouteTo (iAC.GetAddress (1,0), ifInAddrI.GetLocal (), rvector ({1},{1}));//F->E(TCP)
  staticRoutingF->AddHostRouteTo (iBC.GetAddress (1,0), ifInAddrI.GetLocal (), rvector ({1},{1}));//F->E(TCP)
  staticRoutingF->AddHostRouteTo (iCE.GetAddress (0,0), ifInAddrI.GetLocal (), rvector ({1},{1}));//F->E(TCP)
  staticRoutingE->AddHostRouteTo (Ipv4Address ("172.16.1.3"), iFI.GetAddress (1,0), rvector ({1},{1}));//E->C
  staticRoutingE->AddHostRouteTo (Ipv4Address ("172.16.1.3"), ifInAddrI.GetLocal (), rvector ({1},{1}));//E->C(TCP)
  staticRoutingE->AddHostRouteTo (iAC.GetAddress (1,0), ifInAddrI.GetLocal (), rvector ({1},{1}));//E->C(TCP)
  staticRoutingE->AddHostRouteTo (iBC.GetAddress (1,0), ifInAddrI.GetLocal (), rvector ({1},{1}));//E->C(TCP)
  staticRoutingE->AddHostRouteTo (iCE.GetAddress (0,0), ifInAddrI.GetLocal (), rvector ({1},{1}));//E->C(TCP)

  staticRoutingI->AddHostRouteTo (Ipv4Address ("172.16.1.4"), fromLocal, rvector ({1},{1}));//I->F(normal packet)
  staticRoutingI->AddHostRouteTo (Ipv4Address ("172.16.1.4"), iFI.GetAddress (1,0), rvector ({1},{1}));//I->F(TCP)
  staticRoutingI->AddHostRouteTo (Ipv4Address ("172.16.1.4"), ifInAddrI.GetLocal (), rvector ({1},{1}));//I->F(TCP)
  staticRoutingI->AddHostRouteTo (iBD.GetAddress (1,0), ifInAddrI.GetLocal (), rvector ({1},{1}));//I->F(TCP)
  staticRoutingI->AddHostRouteTo (iDF.GetAddress (0,0), ifInAddrI.GetLocal (), rvector ({1},{1}));//I->F(TCP)
  staticRoutingF->AddHostRouteTo (Ipv4Address ("172.16.1.4"), iFI.GetAddress (1,0), rvector ({2},{1}));//F->D
  staticRoutingF->AddHostRouteTo (Ipv4Address ("172.16.1.4"), ifInAddrI.GetLocal (), rvector ({2},{1}));//F->D(TCP)
  staticRoutingF->AddHostRouteTo (iBD.GetAddress (1,0), ifInAddrI.GetLocal (), rvector ({2},{1}));//F->D(TCP)
  staticRoutingF->AddHostRouteTo (iDF.GetAddress (0,0), ifInAddrI.GetLocal (), rvector ({2},{1}));//F->D(TCP)

  staticRoutingI->AddHostRouteTo (Ipv4Address ("172.16.1.5"), fromLocal, rvector ({2},{1}));//I->H(normal packet)
  staticRoutingI->AddHostRouteTo (Ipv4Address ("172.16.1.5"), iHI.GetAddress (1,0), rvector ({2},{1}));//I->H(TCP)
  staticRoutingI->AddHostRouteTo (Ipv4Address ("172.16.1.5"), ifInAddrI.GetLocal (), rvector ({2},{1}));//I->H(TCP)
  staticRoutingI->AddHostRouteTo (iCE.GetAddress (1,0), ifInAddrI.GetLocal (), rvector ({2},{1}));//I->H(TCP)
  staticRoutingI->AddHostRouteTo (iEF.GetAddress (0,0), ifInAddrI.GetLocal (), rvector ({2},{1}));//I->H(TCP)
  staticRoutingI->AddHostRouteTo (iEH.GetAddress (0,0), ifInAddrI.GetLocal (), rvector ({2},{1}));//I->H(TCP)
  staticRoutingH->AddHostRouteTo (Ipv4Address ("172.16.1.5"), iHI.GetAddress (1,0), rvector ({1},{1}));//H->E
  staticRoutingH->AddHostRouteTo (Ipv4Address ("172.16.1.5"), ifInAddrI.GetLocal (), rvector ({1},{1}));//H->E(TCP)
  staticRoutingH->AddHostRouteTo (iCE.GetAddress (1,0), ifInAddrI.GetLocal (), rvector ({1},{1}));//H->E(TCP)
  staticRoutingH->AddHostRouteTo (iEF.GetAddress (0,0), ifInAddrI.GetLocal (), rvector ({1},{1}));//H->E(TCP)
  staticRoutingH->AddHostRouteTo (iEH.GetAddress (0,0), ifInAddrI.GetLocal (), rvector ({1},{1}));//H->E(TCP)

  staticRoutingI->AddHostRouteTo (Ipv4Address ("172.16.1.6"), fromLocal, rvector ({1},{1}));//I->F(normal packet)
  staticRoutingI->AddHostRouteTo (Ipv4Address ("172.16.1.6"), iFI.GetAddress (1,0), rvector ({1},{1}));//I->F(TCP)
  staticRoutingI->AddHostRouteTo (Ipv4Address ("172.16.1.6"), ifInAddrI.GetLocal (), rvector ({1},{1}));//I->F(TCP)
  staticRoutingI->AddHostRouteTo (iEF.GetAddress (1,0), ifInAddrI.GetLocal (), rvector ({1},{1}));//I->F(TCP)
  staticRoutingI->AddHostRouteTo (iDF.GetAddress (1,0), ifInAddrI.GetLocal (), rvector ({1},{1}));//I->F(TCP)
  staticRoutingI->AddHostRouteTo (iFI.GetAddress (0,0), ifInAddrI.GetLocal (), rvector ({1},{1}));//I->F(TCP)

  staticRoutingI->AddHostRouteTo (Ipv4Address ("172.16.1.7"), fromLocal, rvector ({2},{1}));//I->H(normal packet)
  staticRoutingI->AddHostRouteTo (Ipv4Address ("172.16.1.7"), iHI.GetAddress (1,0), rvector ({2},{1}));//I->H(TCP)
  staticRoutingI->AddHostRouteTo (Ipv4Address ("172.16.1.7"), ifInAddrI.GetLocal (), rvector ({2},{1}));//I->H(TCP)
  staticRoutingI->AddHostRouteTo (iGH.GetAddress (0,0), ifInAddrI.GetLocal (), rvector ({2},{1}));//I->H(TCP)
  staticRoutingI->AddHostRouteTo (iGK.GetAddress (0,0), ifInAddrI.GetLocal (), rvector ({2},{1}));//I->H(TCP)
  staticRoutingH->AddHostRouteTo (Ipv4Address ("172.16.1.7"), iHI.GetAddress (1,0), rvector ({2},{1}));//H->G
  staticRoutingH->AddHostRouteTo (Ipv4Address ("172.16.1.7"), ifInAddrI.GetLocal (), rvector ({2},{1}));//H->G(TCP)
  staticRoutingH->AddHostRouteTo (iGH.GetAddress (0,0), ifInAddrI.GetLocal (), rvector ({2},{1}));//H->G(TCP)
  staticRoutingH->AddHostRouteTo (iGK.GetAddress (0,0), ifInAddrI.GetLocal (), rvector ({2},{1}));//H->G(TCP)

  staticRoutingI->AddHostRouteTo (Ipv4Address ("172.16.1.8"), fromLocal, rvector ({2},{1}));//I->H(normal packet)
  staticRoutingI->AddHostRouteTo (Ipv4Address ("172.16.1.8"), iHI.GetAddress (1,0), rvector ({2},{1}));//I->H(TCP)
  staticRoutingI->AddHostRouteTo (Ipv4Address ("172.16.1.8"), ifInAddrI.GetLocal (), rvector ({2},{1}));//I->H(TCP)
  staticRoutingI->AddHostRouteTo (iEH.GetAddress (1,0), ifInAddrI.GetLocal (), rvector ({2},{1}));//I->H(TCP)
  staticRoutingI->AddHostRouteTo (iGH.GetAddress (1,0), ifInAddrI.GetLocal (), rvector ({2},{1}));//I->H(TCP)
  staticRoutingI->AddHostRouteTo (iHI.GetAddress (0,0), ifInAddrI.GetLocal (), rvector ({2},{1}));//I->H(TCP)

  staticRoutingI->AddHostRouteTo (Ipv4Address ("172.16.1.10"), fromLocal, rvector ({3},{1}));//I->J(normal packet)
  staticRoutingI->AddHostRouteTo (Ipv4Address ("172.16.1.10"), iIJ.GetAddress (0,0), rvector ({3},{1}));//I->J(TCP)
  staticRoutingI->AddHostRouteTo (Ipv4Address ("172.16.1.10"), ifInAddrI.GetLocal (), rvector ({3},{1}));//I->J(TCP)
  staticRoutingI->AddHostRouteTo (iIJ.GetAddress (1,0), ifInAddrI.GetLocal (), rvector ({3},{1}));//I->J(TCP)
  staticRoutingI->AddHostRouteTo (iJK.GetAddress (0,0), ifInAddrI.GetLocal (), rvector ({3},{1}));//I->J(TCP)

  staticRoutingI->AddHostRouteTo (Ipv4Address ("172.16.1.11"), fromLocal, rvector ({3},{1}));//I->J(normal packet)
  staticRoutingI->AddHostRouteTo (Ipv4Address ("172.16.1.11"), iIJ.GetAddress (0,0), rvector ({3},{1}));//I->J(TCP)
  staticRoutingI->AddHostRouteTo (Ipv4Address ("172.16.1.11"), ifInAddrI.GetLocal (), rvector ({3},{1}));//I->J(TCP)
  staticRoutingI->AddHostRouteTo (iGK.GetAddress (1,0), ifInAddrI.GetLocal (), rvector ({3},{1}));//I->J(TCP)
  staticRoutingI->AddHostRouteTo (iJK.GetAddress (1,0), ifInAddrI.GetLocal (), rvector ({3},{1}));//I->J(TCP)
  staticRoutingJ->AddHostRouteTo (Ipv4Address ("172.16.1.11"), iIJ.GetAddress (0,0), rvector ({2},{1}));//J->K
  staticRoutingJ->AddHostRouteTo (Ipv4Address ("172.16.1.11"), ifInAddrI.GetLocal (), rvector ({2},{1}));//J->K(TCP)
  staticRoutingJ->AddHostRouteTo (iGK.GetAddress (1,0), ifInAddrI.GetLocal (), rvector ({2},{1}));//J->K(TCP)
  staticRoutingJ->AddHostRouteTo (iJK.GetAddress (1,0), ifInAddrI.GetLocal (), rvector ({2},{1}));//J->K(TCP)

  staticRoutingJ->AddHostRouteTo (Ipv4Address ("172.16.1.1"), fromLocal, rvector ({1},{1}));//J->I(normal packet)
  staticRoutingJ->AddHostRouteTo (Ipv4Address ("172.16.1.1"), iIJ.GetAddress (1,0), rvector ({1},{1}));//J->I(TCP)
  staticRoutingJ->AddHostRouteTo (Ipv4Address ("172.16.1.1"), ifInAddrJ.GetLocal (), rvector ({1},{1}));//J->I(TCP)
  staticRoutingJ->AddHostRouteTo (iAB.GetAddress (0,0), ifInAddrJ.GetLocal (), rvector ({1},{1}));//J->I(TCP)
  staticRoutingJ->AddHostRouteTo (iAC.GetAddress (0,0), ifInAddrJ.GetLocal (), rvector ({1},{1}));//J->I(TCP)
  staticRoutingI->AddHostRouteTo (Ipv4Address ("172.16.1.1"), iIJ.GetAddress (1,0), rvector ({1},{1}));//I->F
  staticRoutingI->AddHostRouteTo (Ipv4Address ("172.16.1.1"), ifInAddrJ.GetLocal (), rvector ({1},{1}));//I->F(TCP)
  staticRoutingI->AddHostRouteTo (iAB.GetAddress (0,0), ifInAddrJ.GetLocal (), rvector ({1},{1}));//I->F(TCP)
  staticRoutingI->AddHostRouteTo (iAC.GetAddress (0,0), ifInAddrJ.GetLocal (), rvector ({1},{1}));//I->F(TCP)
  staticRoutingF->AddHostRouteTo (Ipv4Address ("172.16.1.1"), iIJ.GetAddress (1,0), rvector ({1},{1}));//F->E
  staticRoutingF->AddHostRouteTo (Ipv4Address ("172.16.1.1"), ifInAddrJ.GetLocal (), rvector ({1},{1}));//F->E(TCP)
  staticRoutingF->AddHostRouteTo (iAB.GetAddress (0,0), ifInAddrJ.GetLocal (), rvector ({1},{1}));//F->E(TCP)
  staticRoutingF->AddHostRouteTo (iAC.GetAddress (0,0), ifInAddrJ.GetLocal (), rvector ({1},{1}));//F->E(TCP)
  staticRoutingE->AddHostRouteTo (Ipv4Address ("172.16.1.1"), iIJ.GetAddress (1,0), rvector ({1},{1}));//E->C
  staticRoutingE->AddHostRouteTo (Ipv4Address ("172.16.1.1"), ifInAddrJ.GetLocal (), rvector ({1},{1}));//E->C(TCP)
  staticRoutingE->AddHostRouteTo (iAB.GetAddress (0,0), ifInAddrJ.GetLocal (), rvector ({1},{1}));//E->C(TCP)
  staticRoutingE->AddHostRouteTo (iAC.GetAddress (0,0), ifInAddrJ.GetLocal (), rvector ({1},{1}));//E->C(TCP)
  staticRoutingC->AddHostRouteTo (Ipv4Address ("172.16.1.1"), iIJ.GetAddress (1,0), rvector ({1},{1}));//C->A
  staticRoutingC->AddHostRouteTo (Ipv4Address ("172.16.1.1"), ifInAddrJ.GetLocal (), rvector ({1},{1}));//C->A(TCP)
  staticRoutingC->AddHostRouteTo (iAB.GetAddress (0,0), ifInAddrJ.GetLocal (), rvector ({1},{1}));//C->A(TCP)
  staticRoutingC->AddHostRouteTo (iAC.GetAddress (0,0), ifInAddrJ.GetLocal (), rvector ({1},{1}));//C->A(TCP)

  staticRoutingJ->AddHostRouteTo (Ipv4Address ("172.16.1.2"), fromLocal, rvector ({1},{1}));//J->I(normal packet)
  staticRoutingJ->AddHostRouteTo (Ipv4Address ("172.16.1.2"), iIJ.GetAddress (1,0), rvector ({1},{1}));//J->I(TCP)
  staticRoutingJ->AddHostRouteTo (Ipv4Address ("172.16.1.2"), ifInAddrJ.GetLocal (), rvector ({1},{1}));//J->I(TCP)
  staticRoutingJ->AddHostRouteTo (iAB.GetAddress (1,0), ifInAddrJ.GetLocal (), rvector ({1},{1}));//J->I(TCP)
  staticRoutingJ->AddHostRouteTo (iBC.GetAddress (0,0), ifInAddrJ.GetLocal (), rvector ({1},{1}));//J->I(TCP)
  staticRoutingJ->AddHostRouteTo (iBD.GetAddress (0,0), ifInAddrJ.GetLocal (), rvector ({1},{1}));//J->I(TCP)
  staticRoutingI->AddHostRouteTo (Ipv4Address ("172.16.1.2"), iIJ.GetAddress (1,0), rvector ({1},{1}));//I->F
  staticRoutingI->AddHostRouteTo (Ipv4Address ("172.16.1.2"), ifInAddrJ.GetLocal (), rvector ({1},{1}));//I->F(TCP)
  staticRoutingI->AddHostRouteTo (iAB.GetAddress (1,0), ifInAddrJ.GetLocal (), rvector ({1},{1}));//I->F(TCP)
  staticRoutingI->AddHostRouteTo (iBC.GetAddress (0,0), ifInAddrJ.GetLocal (), rvector ({1},{1}));//I->F(TCP)
  staticRoutingI->AddHostRouteTo (iBD.GetAddress (0,0), ifInAddrJ.GetLocal (), rvector ({1},{1}));//I->F(TCP)
  staticRoutingF->AddHostRouteTo (Ipv4Address ("172.16.1.2"), iIJ.GetAddress (1,0), rvector ({2},{1}));//F->D
  staticRoutingF->AddHostRouteTo (Ipv4Address ("172.16.1.2"), ifInAddrJ.GetLocal (), rvector ({2},{1}));//F->D(TCP)
  staticRoutingF->AddHostRouteTo (iAB.GetAddress (1,0), ifInAddrJ.GetLocal (), rvector ({2},{1}));//F->D(TCP)
  staticRoutingF->AddHostRouteTo (iBC.GetAddress (0,0), ifInAddrJ.GetLocal (), rvector ({2},{1}));//F->D(TCP)
  staticRoutingF->AddHostRouteTo (iBD.GetAddress (0,0), ifInAddrJ.GetLocal (), rvector ({2},{1}));//F->D(TCP)
  staticRoutingD->AddHostRouteTo (Ipv4Address ("172.16.1.2"), iIJ.GetAddress (1,0), rvector ({1},{1}));//D->B
  staticRoutingD->AddHostRouteTo (Ipv4Address ("172.16.1.2"), ifInAddrJ.GetLocal (), rvector ({1},{1}));//D->B(TCP)
  staticRoutingD->AddHostRouteTo (iAB.GetAddress (1,0), ifInAddrJ.GetLocal (), rvector ({1},{1}));//D->B(TCP)
  staticRoutingD->AddHostRouteTo (iBC.GetAddress (0,0), ifInAddrJ.GetLocal (), rvector ({1},{1}));//D->B(TCP)
  staticRoutingD->AddHostRouteTo (iBD.GetAddress (0,0), ifInAddrJ.GetLocal (), rvector ({1},{1}));//D->B(TCP)

  staticRoutingJ->AddHostRouteTo (Ipv4Address ("172.16.1.3"), fromLocal, rvector ({1},{1}));//J->I(normal packet)
  staticRoutingJ->AddHostRouteTo (Ipv4Address ("172.16.1.3"), iIJ.GetAddress (1,0), rvector ({1},{1}));//J->I(TCP)
  staticRoutingJ->AddHostRouteTo (Ipv4Address ("172.16.1.3"), ifInAddrJ.GetLocal (), rvector ({1},{1}));//J->I(TCP)
  staticRoutingJ->AddHostRouteTo (iAC.GetAddress (1,0), ifInAddrJ.GetLocal (), rvector ({1},{1}));//J->I(TCP)
  staticRoutingJ->AddHostRouteTo (iBC.GetAddress (1,0), ifInAddrJ.GetLocal (), rvector ({1},{1}));//J->I(TCP)
  staticRoutingJ->AddHostRouteTo (iCE.GetAddress (0,0), ifInAddrJ.GetLocal (), rvector ({1},{1}));//J->I(TCP)
  staticRoutingI->AddHostRouteTo (Ipv4Address ("172.16.1.3"), iIJ.GetAddress (1,0), rvector ({1},{1}));//I->F
  staticRoutingI->AddHostRouteTo (Ipv4Address ("172.16.1.3"), ifInAddrJ.GetLocal (), rvector ({1},{1}));//I->F(TCP)
  staticRoutingI->AddHostRouteTo (iAC.GetAddress (1,0), ifInAddrJ.GetLocal (), rvector ({1},{1}));//I->F(TCP)
  staticRoutingI->AddHostRouteTo (iBC.GetAddress (1,0), ifInAddrJ.GetLocal (), rvector ({1},{1}));//I->F(TCP)
  staticRoutingI->AddHostRouteTo (iCE.GetAddress (0,0), ifInAddrJ.GetLocal (), rvector ({1},{1}));//I->F(TCP)
  staticRoutingF->AddHostRouteTo (Ipv4Address ("172.16.1.3"), iIJ.GetAddress (1,0), rvector ({1},{1}));//F->E
  staticRoutingF->AddHostRouteTo (Ipv4Address ("172.16.1.3"), ifInAddrJ.GetLocal (), rvector ({1},{1}));//F->E(TCP)
  staticRoutingF->AddHostRouteTo (iAC.GetAddress (1,0), ifInAddrJ.GetLocal (), rvector ({1},{1}));//F->E(TCP)
  staticRoutingF->AddHostRouteTo (iBC.GetAddress (1,0), ifInAddrJ.GetLocal (), rvector ({1},{1}));//F->E(TCP)
  staticRoutingF->AddHostRouteTo (iCE.GetAddress (0,0), ifInAddrJ.GetLocal (), rvector ({1},{1}));//F->E(TCP)
  staticRoutingE->AddHostRouteTo (Ipv4Address ("172.16.1.3"), iIJ.GetAddress (1,0), rvector ({1},{1}));//E->C
  staticRoutingE->AddHostRouteTo (Ipv4Address ("172.16.1.3"), ifInAddrJ.GetLocal (), rvector ({1},{1}));//E->C(TCP)
  staticRoutingE->AddHostRouteTo (iAC.GetAddress (1,0), ifInAddrJ.GetLocal (), rvector ({1},{1}));//E->C(TCP)
  staticRoutingE->AddHostRouteTo (iBC.GetAddress (1,0), ifInAddrJ.GetLocal (), rvector ({1},{1}));//E->C(TCP)
  staticRoutingE->AddHostRouteTo (iCE.GetAddress (0,0), ifInAddrJ.GetLocal (), rvector ({1},{1}));//E->C(TCP)

  staticRoutingJ->AddHostRouteTo (Ipv4Address ("172.16.1.4"), fromLocal, rvector ({1},{1}));//J->I(normal packet)
  staticRoutingJ->AddHostRouteTo (Ipv4Address ("172.16.1.4"), iIJ.GetAddress (1,0), rvector ({1},{1}));//J->I(TCP)
  staticRoutingJ->AddHostRouteTo (Ipv4Address ("172.16.1.4"), ifInAddrJ.GetLocal (), rvector ({1},{1}));//J->I(TCP)
  staticRoutingJ->AddHostRouteTo (iBD.GetAddress (1,0), ifInAddrJ.GetLocal (), rvector ({1},{1}));//J->I(TCP)
  staticRoutingJ->AddHostRouteTo (iDF.GetAddress (0,0), ifInAddrJ.GetLocal (), rvector ({1},{1}));//J->I(TCP)
  staticRoutingI->AddHostRouteTo (Ipv4Address ("172.16.1.4"), iIJ.GetAddress (1,0), rvector ({1},{1}));//I->F
  staticRoutingI->AddHostRouteTo (Ipv4Address ("172.16.1.4"), ifInAddrJ.GetLocal (), rvector ({1},{1}));//I->F(TCP)
  staticRoutingI->AddHostRouteTo (iBD.GetAddress (1,0), ifInAddrJ.GetLocal (), rvector ({1},{1}));//I->F(TCP)
  staticRoutingI->AddHostRouteTo (iDF.GetAddress (0,0), ifInAddrJ.GetLocal (), rvector ({1},{1}));//I->F(TCP)
  staticRoutingF->AddHostRouteTo (Ipv4Address ("172.16.1.4"), iIJ.GetAddress (1,0), rvector ({2},{1}));//F->D
  staticRoutingF->AddHostRouteTo (Ipv4Address ("172.16.1.4"), ifInAddrJ.GetLocal (), rvector ({2},{1}));//F->D(TCP)
  staticRoutingF->AddHostRouteTo (iBD.GetAddress (1,0), ifInAddrJ.GetLocal (), rvector ({2},{1}));//F->D(TCP)
  staticRoutingF->AddHostRouteTo (iDF.GetAddress (0,0), ifInAddrJ.GetLocal (), rvector ({2},{1}));//F->D(TCP)

  staticRoutingJ->AddHostRouteTo (Ipv4Address ("172.16.1.5"), fromLocal, rvector ({1},{1}));//J->I(normal packet)
  staticRoutingJ->AddHostRouteTo (Ipv4Address ("172.16.1.5"), iIJ.GetAddress (1,0), rvector ({1},{1}));//J->I(TCP)
  staticRoutingJ->AddHostRouteTo (Ipv4Address ("172.16.1.5"), ifInAddrJ.GetLocal (), rvector ({1},{1}));//J->I(TCP)
  staticRoutingJ->AddHostRouteTo (iCE.GetAddress (1,0), ifInAddrJ.GetLocal (), rvector ({1},{1}));//J->I(TCP)
  staticRoutingJ->AddHostRouteTo (iEF.GetAddress (0,0), ifInAddrJ.GetLocal (), rvector ({1},{1}));//J->I(TCP)
  staticRoutingJ->AddHostRouteTo (iEH.GetAddress (0,0), ifInAddrJ.GetLocal (), rvector ({1},{1}));//J->I(TCP)
  staticRoutingI->AddHostRouteTo (Ipv4Address ("172.16.1.5"), iIJ.GetAddress (1,0), rvector ({2},{1}));//I->H
  staticRoutingI->AddHostRouteTo (Ipv4Address ("172.16.1.5"), ifInAddrJ.GetLocal (), rvector ({2},{1}));//I->H(TCP)
  staticRoutingI->AddHostRouteTo (iCE.GetAddress (1,0), ifInAddrJ.GetLocal (), rvector ({2},{1}));//I->H(TCP)
  staticRoutingI->AddHostRouteTo (iEF.GetAddress (0,0), ifInAddrJ.GetLocal (), rvector ({2},{1}));//I->H(TCP)
  staticRoutingI->AddHostRouteTo (iEH.GetAddress (0,0), ifInAddrJ.GetLocal (), rvector ({2},{1}));//I->H(TCP)
  staticRoutingH->AddHostRouteTo (Ipv4Address ("172.16.1.5"), iIJ.GetAddress (1,0), rvector ({1},{1}));//H->E
  staticRoutingH->AddHostRouteTo (Ipv4Address ("172.16.1.5"), ifInAddrJ.GetLocal (), rvector ({1},{1}));//H->E(TCP)
  staticRoutingH->AddHostRouteTo (iCE.GetAddress (1,0), ifInAddrJ.GetLocal (), rvector ({1},{1}));//H->E(TCP)
  staticRoutingH->AddHostRouteTo (iEF.GetAddress (0,0), ifInAddrJ.GetLocal (), rvector ({1},{1}));//H->E(TCP)
  staticRoutingH->AddHostRouteTo (iEH.GetAddress (0,0), ifInAddrJ.GetLocal (), rvector ({1},{1}));//H->E(TCP)

  staticRoutingJ->AddHostRouteTo (Ipv4Address ("172.16.1.6"), fromLocal, rvector ({1},{1}));//J->I(normal packet)
  staticRoutingJ->AddHostRouteTo (Ipv4Address ("172.16.1.6"), iIJ.GetAddress (1,0), rvector ({1},{1}));//J->I(TCP)
  staticRoutingJ->AddHostRouteTo (Ipv4Address ("172.16.1.6"), ifInAddrJ.GetLocal (), rvector ({1},{1}));//J->I(TCP)
  staticRoutingJ->AddHostRouteTo (iEF.GetAddress (1,0), ifInAddrJ.GetLocal (), rvector ({1},{1}));//J->I(TCP)
  staticRoutingJ->AddHostRouteTo (iDF.GetAddress (1,0), ifInAddrJ.GetLocal (), rvector ({1},{1}));//J->I(TCP)
  staticRoutingJ->AddHostRouteTo (iFI.GetAddress (0,0), ifInAddrJ.GetLocal (), rvector ({1},{1}));//J->I(TCP)
  staticRoutingI->AddHostRouteTo (Ipv4Address ("172.16.1.6"), iIJ.GetAddress (1,0), rvector ({1},{1}));//I->F
  staticRoutingI->AddHostRouteTo (Ipv4Address ("172.16.1.6"), ifInAddrJ.GetLocal (), rvector ({1},{1}));//I->F(TCP)
  staticRoutingI->AddHostRouteTo (iEF.GetAddress (1,0), ifInAddrJ.GetLocal (), rvector ({1},{1}));//I->F(TCP)
  staticRoutingI->AddHostRouteTo (iDF.GetAddress (1,0), ifInAddrJ.GetLocal (), rvector ({1},{1}));//I->F(TCP)
  staticRoutingI->AddHostRouteTo (iFI.GetAddress (0,0), ifInAddrJ.GetLocal (), rvector ({1},{1}));//I->F(TCP)

  staticRoutingJ->AddHostRouteTo (Ipv4Address ("172.16.1.7"), fromLocal, rvector ({2},{1}));//J->K(normal packet)
  staticRoutingJ->AddHostRouteTo (Ipv4Address ("172.16.1.7"), iJK.GetAddress (0,0), rvector ({2},{1}));//J->K(TCP)
  staticRoutingJ->AddHostRouteTo (Ipv4Address ("172.16.1.7"), ifInAddrJ.GetLocal (), rvector ({2},{1}));//J->K(TCP)
  staticRoutingJ->AddHostRouteTo (iGH.GetAddress (0,0), ifInAddrJ.GetLocal (), rvector ({2},{1}));//J->K(TCP)
  staticRoutingJ->AddHostRouteTo (iGK.GetAddress (0,0), ifInAddrJ.GetLocal (), rvector ({2},{1}));//J->K(TCP)
  staticRoutingK->AddHostRouteTo (Ipv4Address ("172.16.1.7"), iJK.GetAddress (0,0), rvector ({1},{1}));//K->G
  staticRoutingK->AddHostRouteTo (Ipv4Address ("172.16.1.7"), ifInAddrJ.GetLocal (), rvector ({1},{1}));//K->G(TCP)
  staticRoutingK->AddHostRouteTo (iGH.GetAddress (0,0), ifInAddrJ.GetLocal (), rvector ({1},{1}));//K->G(TCP)
  staticRoutingK->AddHostRouteTo (iGK.GetAddress (0,0), ifInAddrJ.GetLocal (), rvector ({1},{1}));//K->G(TCP)

  staticRoutingJ->AddHostRouteTo (Ipv4Address ("172.16.1.8"), fromLocal, rvector ({1},{1}));//J->I(normal packet)
  staticRoutingJ->AddHostRouteTo (Ipv4Address ("172.16.1.8"), iIJ.GetAddress (1,0), rvector ({1},{1}));//J->I(TCP)
  staticRoutingJ->AddHostRouteTo (Ipv4Address ("172.16.1.8"), ifInAddrJ.GetLocal (), rvector ({1},{1}));//J->I(TCP)
  staticRoutingJ->AddHostRouteTo (iEH.GetAddress (1,0), ifInAddrJ.GetLocal (), rvector ({1},{1}));//J->I(TCP)
  staticRoutingJ->AddHostRouteTo (iGH.GetAddress (1,0), ifInAddrJ.GetLocal (), rvector ({1},{1}));//J->I(TCP)
  staticRoutingJ->AddHostRouteTo (iHI.GetAddress (0,0), ifInAddrJ.GetLocal (), rvector ({1},{1}));//J->I(TCP)
  staticRoutingI->AddHostRouteTo (Ipv4Address ("172.16.1.8"), iIJ.GetAddress (1,0), rvector ({2},{1}));//I->H
  staticRoutingI->AddHostRouteTo (Ipv4Address ("172.16.1.8"), ifInAddrJ.GetLocal (), rvector ({2},{1}));//I->H(TCP)
  staticRoutingI->AddHostRouteTo (iEH.GetAddress (1,0), ifInAddrJ.GetLocal (), rvector ({2},{1}));//I->H(TCP)
  staticRoutingI->AddHostRouteTo (iGH.GetAddress (1,0), ifInAddrJ.GetLocal (), rvector ({2},{1}));//I->H(TCP)
  staticRoutingI->AddHostRouteTo (iHI.GetAddress (0,0), ifInAddrJ.GetLocal (), rvector ({2},{1}));//I->H(TCP)

  staticRoutingJ->AddHostRouteTo (Ipv4Address ("172.16.1.9"), fromLocal, rvector ({1},{1}));//J->I(normal packet)
  staticRoutingJ->AddHostRouteTo (Ipv4Address ("172.16.1.9"), iIJ.GetAddress (1,0), rvector ({1},{1}));//J->I(TCP)
  staticRoutingJ->AddHostRouteTo (Ipv4Address ("172.16.1.9"), ifInAddrJ.GetLocal (), rvector ({1},{1}));//J->I(TCP)
  staticRoutingJ->AddHostRouteTo (iFI.GetAddress (1,0), ifInAddrJ.GetLocal (), rvector ({1},{1}));//J->I(TCP)
  staticRoutingJ->AddHostRouteTo (iHI.GetAddress (1,0), ifInAddrJ.GetLocal (), rvector ({1},{1}));//J->I(TCP)
  staticRoutingJ->AddHostRouteTo (iIJ.GetAddress (0,0), ifInAddrJ.GetLocal (), rvector ({1},{1}));//J->I(TCP)

  staticRoutingJ->AddHostRouteTo (Ipv4Address ("172.16.1.11"), fromLocal, rvector ({2},{1}));//J->K(normal packet)
  staticRoutingJ->AddHostRouteTo (Ipv4Address ("172.16.1.11"), iJK.GetAddress (0,0), rvector ({2},{1}));//J->K(TCP)
  staticRoutingJ->AddHostRouteTo (Ipv4Address ("172.16.1.11"), ifInAddrJ.GetLocal (), rvector ({2},{1}));//J->K(TCP)
  staticRoutingJ->AddHostRouteTo (iGK.GetAddress (1,0), ifInAddrJ.GetLocal (), rvector ({2},{1}));//J->K(TCP)
  staticRoutingJ->AddHostRouteTo (iJK.GetAddress (1,0), ifInAddrJ.GetLocal (), rvector ({2},{1}));//J->K(TCP)

  staticRoutingK->AddHostRouteTo (Ipv4Address ("172.16.1.2"), fromLocal, rvector ({2},{1}));//K->J(normal packet)
  staticRoutingK->AddHostRouteTo (Ipv4Address ("172.16.1.2"), iJK.GetAddress (1,0), rvector ({2},{1}));//K->J(TCP)
  staticRoutingK->AddHostRouteTo (Ipv4Address ("172.16.1.2"), ifInAddrK.GetLocal (), rvector ({2},{1}));//K->J(TCP)
  staticRoutingK->AddHostRouteTo (iAB.GetAddress (1,0), ifInAddrK.GetLocal (), rvector ({2},{1}));//K->J(TCP)
  staticRoutingK->AddHostRouteTo (iBC.GetAddress (0,0), ifInAddrK.GetLocal (), rvector ({2},{1}));//K->J(TCP)
  staticRoutingK->AddHostRouteTo (iBD.GetAddress (0,0), ifInAddrK.GetLocal (), rvector ({2},{1}));//K->J(TCP)
  staticRoutingJ->AddHostRouteTo (Ipv4Address ("172.16.1.2"), iJK.GetAddress (1,0), rvector ({1},{1}));//J->I
  staticRoutingJ->AddHostRouteTo (Ipv4Address ("172.16.1.2"), ifInAddrK.GetLocal (), rvector ({1},{1}));//J->I(TCP)
  staticRoutingJ->AddHostRouteTo (iAB.GetAddress (1,0), ifInAddrK.GetLocal (), rvector ({1},{1}));//J->I(TCP)
  staticRoutingJ->AddHostRouteTo (iBC.GetAddress (0,0), ifInAddrK.GetLocal (), rvector ({1},{1}));//J->I(TCP)
  staticRoutingJ->AddHostRouteTo (iBD.GetAddress (0,0), ifInAddrK.GetLocal (), rvector ({1},{1}));//J->I(TCP)
  staticRoutingI->AddHostRouteTo (Ipv4Address ("172.16.1.2"), iJK.GetAddress (1,0), rvector ({1},{1}));//I->F
  staticRoutingI->AddHostRouteTo (Ipv4Address ("172.16.1.2"), ifInAddrK.GetLocal (), rvector ({1},{1}));//I->F(TCP)
  staticRoutingI->AddHostRouteTo (iAB.GetAddress (1,0), ifInAddrK.GetLocal (), rvector ({1},{1}));//I->F(TCP)
  staticRoutingI->AddHostRouteTo (iBC.GetAddress (0,0), ifInAddrK.GetLocal (), rvector ({1},{1}));//I->F(TCP)
  staticRoutingI->AddHostRouteTo (iBD.GetAddress (0,0), ifInAddrK.GetLocal (), rvector ({1},{1}));//I->F(TCP)
  staticRoutingF->AddHostRouteTo (Ipv4Address ("172.16.1.2"), iJK.GetAddress (1,0), rvector ({2},{1}));//F->D
  staticRoutingF->AddHostRouteTo (Ipv4Address ("172.16.1.2"), ifInAddrK.GetLocal (), rvector ({2},{1}));//F->D(TCP)
  staticRoutingF->AddHostRouteTo (iAB.GetAddress (1,0), ifInAddrK.GetLocal (), rvector ({2},{1}));//F->D(TCP)
  staticRoutingF->AddHostRouteTo (iBC.GetAddress (0,0), ifInAddrK.GetLocal (), rvector ({2},{1}));//F->D(TCP)
  staticRoutingF->AddHostRouteTo (iBD.GetAddress (0,0), ifInAddrK.GetLocal (), rvector ({2},{1}));//F->D(TCP)
  staticRoutingD->AddHostRouteTo (Ipv4Address ("172.16.1.2"), iJK.GetAddress (1,0), rvector ({1},{1}));//D->B
  staticRoutingD->AddHostRouteTo (Ipv4Address ("172.16.1.2"), ifInAddrK.GetLocal (), rvector ({1},{1}));//D->B(TCP)
  staticRoutingD->AddHostRouteTo (iAB.GetAddress (1,0), ifInAddrK.GetLocal (), rvector ({1},{1}));//D->B(TCP)
  staticRoutingD->AddHostRouteTo (iBC.GetAddress (0,0), ifInAddrK.GetLocal (), rvector ({1},{1}));//D->B(TCP)
  staticRoutingD->AddHostRouteTo (iBD.GetAddress (0,0), ifInAddrK.GetLocal (), rvector ({1},{1}));//D->B(TCP)

  staticRoutingK->AddHostRouteTo (Ipv4Address ("172.16.1.3"), fromLocal, rvector ({1},{1}));//K->G(normal packet)
  staticRoutingK->AddHostRouteTo (Ipv4Address ("172.16.1.3"), iGK.GetAddress (1,0), rvector ({1},{1}));//K->G(TCP)
  staticRoutingK->AddHostRouteTo (Ipv4Address ("172.16.1.3"), ifInAddrK.GetLocal (), rvector ({1},{1}));//K->G(TCP)
  staticRoutingK->AddHostRouteTo (iAC.GetAddress (1,0), ifInAddrK.GetLocal (), rvector ({1},{1}));//K->G(TCP)
  staticRoutingK->AddHostRouteTo (iBC.GetAddress (1,0), ifInAddrK.GetLocal (), rvector ({1},{1}));//K->G(TCP)
  staticRoutingK->AddHostRouteTo (iCE.GetAddress (0,0), ifInAddrK.GetLocal (), rvector ({1},{1}));//K->G(TCP)
  staticRoutingG->AddHostRouteTo (Ipv4Address ("172.16.1.3"), iGK.GetAddress (1,0), rvector ({1},{1}));//G->H
  staticRoutingG->AddHostRouteTo (Ipv4Address ("172.16.1.3"), ifInAddrK.GetLocal (), rvector ({1},{1}));//G->H(TCP)
  staticRoutingG->AddHostRouteTo (iAC.GetAddress (1,0), ifInAddrK.GetLocal (), rvector ({1},{1}));//G->H(TCP)
  staticRoutingG->AddHostRouteTo (iBC.GetAddress (1,0), ifInAddrK.GetLocal (), rvector ({1},{1}));//G->H(TCP)
  staticRoutingG->AddHostRouteTo (iCE.GetAddress (0,0), ifInAddrK.GetLocal (), rvector ({1},{1}));//G->H(TCP)
  staticRoutingH->AddHostRouteTo (Ipv4Address ("172.16.1.3"), iGK.GetAddress (1,0), rvector ({1},{1}));//H->E
  staticRoutingH->AddHostRouteTo (Ipv4Address ("172.16.1.3"), ifInAddrK.GetLocal (), rvector ({1},{1}));//H->E(TCP)
  staticRoutingH->AddHostRouteTo (iAC.GetAddress (1,0), ifInAddrK.GetLocal (), rvector ({1},{1}));//H->E(TCP)
  staticRoutingH->AddHostRouteTo (iBC.GetAddress (1,0), ifInAddrK.GetLocal (), rvector ({1},{1}));//H->E(TCP)
  staticRoutingH->AddHostRouteTo (iCE.GetAddress (0,0), ifInAddrK.GetLocal (), rvector ({1},{1}));//H->E(TCP)
  staticRoutingE->AddHostRouteTo (Ipv4Address ("172.16.1.3"), iGK.GetAddress (1,0), rvector ({1},{1}));//E->C
  staticRoutingE->AddHostRouteTo (Ipv4Address ("172.16.1.3"), ifInAddrK.GetLocal (), rvector ({1},{1}));//E->C(TCP)
  staticRoutingE->AddHostRouteTo (iAC.GetAddress (1,0), ifInAddrK.GetLocal (), rvector ({1},{1}));//E->C(TCP)
  staticRoutingE->AddHostRouteTo (iBC.GetAddress (1,0), ifInAddrK.GetLocal (), rvector ({1},{1}));//E->C(TCP)
  staticRoutingE->AddHostRouteTo (iCE.GetAddress (0,0), ifInAddrK.GetLocal (), rvector ({1},{1}));//E->C(TCP)

  staticRoutingK->AddHostRouteTo (Ipv4Address ("172.16.1.4"), fromLocal, rvector ({2},{1}));//K->J(normal packet)
  staticRoutingK->AddHostRouteTo (Ipv4Address ("172.16.1.4"), iJK.GetAddress (1,0), rvector ({2},{1}));//K->J(TCP)
  staticRoutingK->AddHostRouteTo (Ipv4Address ("172.16.1.4"), ifInAddrK.GetLocal (), rvector ({2},{1}));//K->J(TCP)
  staticRoutingK->AddHostRouteTo (iBD.GetAddress (1,0), ifInAddrK.GetLocal (), rvector ({2},{1}));//K->J(TCP)
  staticRoutingK->AddHostRouteTo (iDF.GetAddress (0,0), ifInAddrK.GetLocal (), rvector ({2},{1}));//K->J(TCP)
  staticRoutingJ->AddHostRouteTo (Ipv4Address ("172.16.1.4"), iJK.GetAddress (1,0), rvector ({1},{1}));//J->I
  staticRoutingJ->AddHostRouteTo (Ipv4Address ("172.16.1.4"), ifInAddrK.GetLocal (), rvector ({1},{1}));//J->I(TCP)
  staticRoutingJ->AddHostRouteTo (iBD.GetAddress (1,0), ifInAddrK.GetLocal (), rvector ({1},{1}));//J->I(TCP)
  staticRoutingJ->AddHostRouteTo (iDF.GetAddress (0,0), ifInAddrK.GetLocal (), rvector ({1},{1}));//J->I(TCP)
  staticRoutingI->AddHostRouteTo (Ipv4Address ("172.16.1.4"), iJK.GetAddress (1,0), rvector ({1},{1}));//I->F
  staticRoutingI->AddHostRouteTo (Ipv4Address ("172.16.1.4"), ifInAddrK.GetLocal (), rvector ({1},{1}));//I->F(TCP)
  staticRoutingI->AddHostRouteTo (iBD.GetAddress (1,0), ifInAddrK.GetLocal (), rvector ({1},{1}));//I->F(TCP)
  staticRoutingI->AddHostRouteTo (iDF.GetAddress (0,0), ifInAddrK.GetLocal (), rvector ({1},{1}));//I->F(TCP)
  staticRoutingF->AddHostRouteTo (Ipv4Address ("172.16.1.4"), iJK.GetAddress (1,0), rvector ({2},{1}));//F->D
  staticRoutingF->AddHostRouteTo (Ipv4Address ("172.16.1.4"), ifInAddrK.GetLocal (), rvector ({2},{1}));//F->D(TCP)
  staticRoutingF->AddHostRouteTo (iBD.GetAddress (1,0), ifInAddrK.GetLocal (), rvector ({2},{1}));//F->D(TCP)
  staticRoutingF->AddHostRouteTo (iDF.GetAddress (0,0), ifInAddrK.GetLocal (), rvector ({2},{1}));//F->D(TCP)

  staticRoutingK->AddHostRouteTo (Ipv4Address ("172.16.1.5"), fromLocal, rvector ({1},{1}));//K->G(normal packet)
  staticRoutingK->AddHostRouteTo (Ipv4Address ("172.16.1.5"), iGK.GetAddress (1,0), rvector ({1},{1}));//K->G(TCP)
  staticRoutingK->AddHostRouteTo (Ipv4Address ("172.16.1.5"), ifInAddrK.GetLocal (), rvector ({1},{1}));//K->G(TCP)
  staticRoutingK->AddHostRouteTo (iCE.GetAddress (1,0), ifInAddrK.GetLocal (), rvector ({1},{1}));//K->G(TCP)
  staticRoutingK->AddHostRouteTo (iEF.GetAddress (0,0), ifInAddrK.GetLocal (), rvector ({1},{1}));//K->G(TCP)
  staticRoutingK->AddHostRouteTo (iEH.GetAddress (0,0), ifInAddrK.GetLocal (), rvector ({1},{1}));//K->G(TCP)
  staticRoutingG->AddHostRouteTo (Ipv4Address ("172.16.1.5"), iGK.GetAddress (1,0), rvector ({1},{1}));//G->H
  staticRoutingG->AddHostRouteTo (Ipv4Address ("172.16.1.5"), ifInAddrK.GetLocal (), rvector ({1},{1}));//G->H(TCP)
  staticRoutingG->AddHostRouteTo (iCE.GetAddress (1,0), ifInAddrK.GetLocal (), rvector ({1},{1}));//G->H(TCP)
  staticRoutingG->AddHostRouteTo (iEF.GetAddress (0,0), ifInAddrK.GetLocal (), rvector ({1},{1}));//G->H(TCP)
  staticRoutingG->AddHostRouteTo (iEH.GetAddress (0,0), ifInAddrK.GetLocal (), rvector ({1},{1}));//G->H(TCP)
  staticRoutingH->AddHostRouteTo (Ipv4Address ("172.16.1.5"), iGK.GetAddress (1,0), rvector ({1},{1}));//H->E
  staticRoutingH->AddHostRouteTo (Ipv4Address ("172.16.1.5"), ifInAddrK.GetLocal (), rvector ({1},{1}));//H->E(TCP)
  staticRoutingH->AddHostRouteTo (iCE.GetAddress (1,0), ifInAddrK.GetLocal (), rvector ({1},{1}));//H->E(TCP)
  staticRoutingH->AddHostRouteTo (iEF.GetAddress (0,0), ifInAddrK.GetLocal (), rvector ({1},{1}));//H->E(TCP)
  staticRoutingH->AddHostRouteTo (iEH.GetAddress (0,0), ifInAddrK.GetLocal (), rvector ({1},{1}));//H->E(TCP)

  staticRoutingK->AddHostRouteTo (Ipv4Address ("172.16.1.6"), fromLocal, rvector ({2},{1}));//K->J(normal packet)
  staticRoutingK->AddHostRouteTo (Ipv4Address ("172.16.1.6"), iJK.GetAddress (1,0), rvector ({2},{1}));//K->J(TCP)
  staticRoutingK->AddHostRouteTo (Ipv4Address ("172.16.1.6"), ifInAddrK.GetLocal (), rvector ({2},{1}));//K->J(TCP)
  staticRoutingK->AddHostRouteTo (iEF.GetAddress (1,0), ifInAddrK.GetLocal (), rvector ({2},{1}));//K->J(TCP)
  staticRoutingK->AddHostRouteTo (iDF.GetAddress (1,0), ifInAddrK.GetLocal (), rvector ({2},{1}));//K->J(TCP)
  staticRoutingK->AddHostRouteTo (iFI.GetAddress (0,0), ifInAddrK.GetLocal (), rvector ({2},{1}));//K->J(TCP)
  staticRoutingJ->AddHostRouteTo (Ipv4Address ("172.16.1.6"), iJK.GetAddress (1,0), rvector ({1},{1}));//J->I
  staticRoutingJ->AddHostRouteTo (Ipv4Address ("172.16.1.6"), ifInAddrK.GetLocal (), rvector ({1},{1}));//J->I(TCP)
  staticRoutingJ->AddHostRouteTo (iEF.GetAddress (1,0), ifInAddrK.GetLocal (), rvector ({1},{1}));//J->I(TCP)
  staticRoutingJ->AddHostRouteTo (iDF.GetAddress (1,0), ifInAddrK.GetLocal (), rvector ({1},{1}));//J->I(TCP)
  staticRoutingJ->AddHostRouteTo (iFI.GetAddress (0,0), ifInAddrK.GetLocal (), rvector ({1},{1}));//J->I(TCP)
  staticRoutingI->AddHostRouteTo (Ipv4Address ("172.16.1.6"), iJK.GetAddress (1,0), rvector ({1},{1}));//I->F
  staticRoutingI->AddHostRouteTo (Ipv4Address ("172.16.1.6"), ifInAddrK.GetLocal (), rvector ({1},{1}));//I->F(TCP)
  staticRoutingI->AddHostRouteTo (iEF.GetAddress (1,0), ifInAddrK.GetLocal (), rvector ({1},{1}));//I->F(TCP)
  staticRoutingI->AddHostRouteTo (iDF.GetAddress (1,0), ifInAddrK.GetLocal (), rvector ({1},{1}));//I->F(TCP)
  staticRoutingI->AddHostRouteTo (iFI.GetAddress (0,0), ifInAddrK.GetLocal (), rvector ({1},{1}));//I->F(TCP)

  staticRoutingK->AddHostRouteTo (Ipv4Address ("172.16.1.7"), fromLocal, rvector ({1},{1}));//K->G(normal packet)
  staticRoutingK->AddHostRouteTo (Ipv4Address ("172.16.1.7"), iGK.GetAddress (1,0), rvector ({1},{1}));//K->G(TCP)
  staticRoutingK->AddHostRouteTo (Ipv4Address ("172.16.1.7"), ifInAddrK.GetLocal (), rvector ({1},{1}));//K->G(TCP)
  staticRoutingK->AddHostRouteTo (iGH.GetAddress (0,0), ifInAddrK.GetLocal (), rvector ({1},{1}));//K->G(TCP)
  staticRoutingK->AddHostRouteTo (iGK.GetAddress (0,0), ifInAddrK.GetLocal (), rvector ({1},{1}));//K->G(TCP)

  staticRoutingK->AddHostRouteTo (Ipv4Address ("172.16.1.8"), fromLocal, rvector ({1},{1}));//K->G(normal packet)
  staticRoutingK->AddHostRouteTo (Ipv4Address ("172.16.1.8"), iGK.GetAddress (1,0), rvector ({1},{1}));//K->G(TCP)
  staticRoutingK->AddHostRouteTo (Ipv4Address ("172.16.1.8"), ifInAddrK.GetLocal (), rvector ({1},{1}));//K->G(TCP)
  staticRoutingK->AddHostRouteTo (iEH.GetAddress (1,0), ifInAddrK.GetLocal (), rvector ({1},{1}));//K->G(TCP)
  staticRoutingK->AddHostRouteTo (iGH.GetAddress (1,0), ifInAddrK.GetLocal (), rvector ({1},{1}));//K->G(TCP)
  staticRoutingK->AddHostRouteTo (iHI.GetAddress (0,0), ifInAddrK.GetLocal (), rvector ({1},{1}));//K->G(TCP)
  staticRoutingG->AddHostRouteTo (Ipv4Address ("172.16.1.8"), iGK.GetAddress (1,0), rvector ({1},{1}));//G->H
  staticRoutingG->AddHostRouteTo (Ipv4Address ("172.16.1.8"), ifInAddrK.GetLocal (), rvector ({1},{1}));//G->H(TCP)
  staticRoutingG->AddHostRouteTo (iEH.GetAddress (1,0), ifInAddrK.GetLocal (), rvector ({1},{1}));//G->H(TCP)
  staticRoutingG->AddHostRouteTo (iGH.GetAddress (1,0), ifInAddrK.GetLocal (), rvector ({1},{1}));//G->H(TCP)
  staticRoutingG->AddHostRouteTo (iHI.GetAddress (0,0), ifInAddrK.GetLocal (), rvector ({1},{1}));//G->H(TCP)

  staticRoutingK->AddHostRouteTo (Ipv4Address ("172.16.1.9"), fromLocal, rvector ({2},{1}));//K->J(normal packet)
  staticRoutingK->AddHostRouteTo (Ipv4Address ("172.16.1.9"), iJK.GetAddress (1,0), rvector ({2},{1}));//K->J(TCP)
  staticRoutingK->AddHostRouteTo (Ipv4Address ("172.16.1.9"), ifInAddrK.GetLocal (), rvector ({2},{1}));//K->J(TCP)
  staticRoutingK->AddHostRouteTo (iFI.GetAddress (1,0), ifInAddrK.GetLocal (), rvector ({2},{1}));//K->J(TCP)
  staticRoutingK->AddHostRouteTo (iHI.GetAddress (1,0), ifInAddrK.GetLocal (), rvector ({2},{1}));//K->J(TCP)
  staticRoutingK->AddHostRouteTo (iIJ.GetAddress (0,0), ifInAddrK.GetLocal (), rvector ({2},{1}));//K->J(TCP)
  staticRoutingJ->AddHostRouteTo (Ipv4Address ("172.16.1.9"), iJK.GetAddress (1,0), rvector ({1},{1}));//J->I
  staticRoutingJ->AddHostRouteTo (Ipv4Address ("172.16.1.9"), ifInAddrK.GetLocal (), rvector ({1},{1}));//J->I(TCP)
  staticRoutingJ->AddHostRouteTo (iFI.GetAddress (1,0), ifInAddrK.GetLocal (), rvector ({1},{1}));//J->I(TCP)
  staticRoutingJ->AddHostRouteTo (iHI.GetAddress (1,0), ifInAddrK.GetLocal (), rvector ({1},{1}));//J->I(TCP)
  staticRoutingJ->AddHostRouteTo (iIJ.GetAddress (0,0), ifInAddrK.GetLocal (), rvector ({1},{1}));//J->I(TCP)

  staticRoutingK->AddHostRouteTo (Ipv4Address ("172.16.1.10"), fromLocal, rvector ({2},{1}));//K->J(normal packet)
  staticRoutingK->AddHostRouteTo (Ipv4Address ("172.16.1.10"), iJK.GetAddress (1,0), rvector ({2},{1}));//K->J(TCP)
  staticRoutingK->AddHostRouteTo (Ipv4Address ("172.16.1.10"), ifInAddrK.GetLocal (), rvector ({2},{1}));//K->J(TCP)
  staticRoutingK->AddHostRouteTo (iIJ.GetAddress (1,0), ifInAddrK.GetLocal (), rvector ({2},{1}));//K->J(TCP)
  staticRoutingK->AddHostRouteTo (iJK.GetAddress (0,0), ifInAddrK.GetLocal (), rvector ({2},{1}));//K->J(TCP)



  staticRoutingB->AddHostRouteTo (Ipv4Address ("172.16.1.11"), fromLocal, rvector ({2,2},{0.275,0.725}));//B->C,D(normal packet)
  staticRoutingB->AddHostRouteTo (Ipv4Address ("172.16.1.11"), iBC.GetAddress (0,0), rvector ({2,2},{0.275,0.725}));//B->C,D(TCP)
  staticRoutingB->AddHostRouteTo (Ipv4Address ("172.16.1.11"), iBD.GetAddress (0,0), rvector ({2,2},{0.275,0.725}));//B->C,D(TCP)
  staticRoutingB->AddHostRouteTo (Ipv4Address ("172.16.1.11"), ifInAddrB.GetLocal (), rvector ({2,2},{0.275,0.725}));//B->C,D(TCP)
  staticRoutingB->AddHostRouteTo (iGK.GetAddress (1,0), ifInAddrB.GetLocal (), rvector ({2,2},{0.275,0.725}));//B->C,D(TCP)
  staticRoutingB->AddHostRouteTo (iJK.GetAddress (1,0), ifInAddrB.GetLocal (), rvector ({2,2},{0.275,0.725}));//B->C,D(TCP)
  staticRoutingC->AddHostRouteTo (Ipv4Address ("172.16.1.11"), iBC.GetAddress (0,0), rvector ({3},{1}));//C->E
  staticRoutingC->AddHostRouteTo (Ipv4Address ("172.16.1.11"), iBD.GetAddress (0,0), rvector ({3},{1}));//C->E
  staticRoutingC->AddHostRouteTo (Ipv4Address ("172.16.1.11"), ifInAddrB.GetLocal (), rvector ({3},{1}));//C->E(TCP)
  staticRoutingC->AddHostRouteTo (iGK.GetAddress (1,0), ifInAddrB.GetLocal (), rvector ({3},{1}));//C->E(TCP)
  staticRoutingC->AddHostRouteTo (iJK.GetAddress (1,0), ifInAddrB.GetLocal (), rvector ({3},{1}));//C->E(TCP)
  staticRoutingE->AddHostRouteTo (Ipv4Address ("172.16.1.11"), iBC.GetAddress (0,0), rvector ({3},{1}));//E->H
  staticRoutingE->AddHostRouteTo (Ipv4Address ("172.16.1.11"), iBD.GetAddress (0,0), rvector ({3},{1}));//E->H
  staticRoutingE->AddHostRouteTo (Ipv4Address ("172.16.1.11"), ifInAddrB.GetLocal (), rvector ({3},{1}));//E->H(TCP)
  staticRoutingE->AddHostRouteTo (iGK.GetAddress (1,0), ifInAddrB.GetLocal (), rvector ({3},{1}));//E->H(TCP)
  staticRoutingE->AddHostRouteTo (iJK.GetAddress (1,0), ifInAddrB.GetLocal (), rvector ({3},{1}));//E->H(TCP)
  staticRoutingH->AddHostRouteTo (Ipv4Address ("172.16.1.11"), iBC.GetAddress (0,0), rvector ({2},{1}));//H->G
  staticRoutingH->AddHostRouteTo (Ipv4Address ("172.16.1.11"), iBD.GetAddress (0,0), rvector ({2},{1}));//H->G
  staticRoutingH->AddHostRouteTo (Ipv4Address ("172.16.1.11"), ifInAddrB.GetLocal (), rvector ({2},{1}));//H->G(TCP)
  staticRoutingH->AddHostRouteTo (iGK.GetAddress (1,0), ifInAddrB.GetLocal (), rvector ({2},{1}));//H->G(TCP)
  staticRoutingH->AddHostRouteTo (iJK.GetAddress (1,0), ifInAddrB.GetLocal (), rvector ({2},{1}));//H->G(TCP)
  staticRoutingG->AddHostRouteTo (Ipv4Address ("172.16.1.11"), iBC.GetAddress (0,0), rvector ({2},{1}));//G->K
  staticRoutingG->AddHostRouteTo (Ipv4Address ("172.16.1.11"), iBD.GetAddress (0,0), rvector ({2},{1}));//G->K
  staticRoutingG->AddHostRouteTo (Ipv4Address ("172.16.1.11"), ifInAddrB.GetLocal (), rvector ({2},{1}));//G->K(TCP)
  staticRoutingG->AddHostRouteTo (iGK.GetAddress (1,0), ifInAddrB.GetLocal (), rvector ({2},{1}));//G->K(TCP)
  staticRoutingG->AddHostRouteTo (iJK.GetAddress (1,0), ifInAddrB.GetLocal (), rvector ({2},{1}));//G->K(TCP)
  staticRoutingD->AddHostRouteTo (Ipv4Address ("172.16.1.11"), iBD.GetAddress (0,0), rvector ({2},{1}));//D->F
  staticRoutingD->AddHostRouteTo (Ipv4Address ("172.16.1.11"), iBC.GetAddress (0,0), rvector ({2},{1}));//D->F
  staticRoutingD->AddHostRouteTo (Ipv4Address ("172.16.1.11"), ifInAddrB.GetLocal (), rvector ({2},{1}));//D->F(TCP)
  staticRoutingD->AddHostRouteTo (iGK.GetAddress (1,0), ifInAddrB.GetLocal (), rvector ({2},{1}));//D->F(TCP)
  staticRoutingD->AddHostRouteTo (iJK.GetAddress (1,0), ifInAddrB.GetLocal (), rvector ({2},{1}));//D->F(TCP)
  staticRoutingF->AddHostRouteTo (Ipv4Address ("172.16.1.11"), iBD.GetAddress (0,0), rvector ({3},{1}));//F->I
  staticRoutingF->AddHostRouteTo (Ipv4Address ("172.16.1.11"), iBC.GetAddress (0,0), rvector ({3},{1}));//F->I
  staticRoutingF->AddHostRouteTo (Ipv4Address ("172.16.1.11"), ifInAddrB.GetLocal (), rvector ({3},{1}));//F->I(TCP)
  staticRoutingF->AddHostRouteTo (iGK.GetAddress (1,0), ifInAddrB.GetLocal (), rvector ({3},{1}));//F->I(TCP)
  staticRoutingF->AddHostRouteTo (iJK.GetAddress (1,0), ifInAddrB.GetLocal (), rvector ({3},{1}));//F->I(TCP)
  staticRoutingI->AddHostRouteTo (Ipv4Address ("172.16.1.11"), iBD.GetAddress (0,0), rvector ({3},{1}));//I->J
  staticRoutingI->AddHostRouteTo (Ipv4Address ("172.16.1.11"), iBC.GetAddress (0,0), rvector ({3},{1}));//I->J
  staticRoutingI->AddHostRouteTo (Ipv4Address ("172.16.1.11"), ifInAddrB.GetLocal (), rvector ({3},{1}));//I->J(TCP)
  staticRoutingI->AddHostRouteTo (iGK.GetAddress (1,0), ifInAddrB.GetLocal (), rvector ({3},{1}));//I->J(TCP)
  staticRoutingI->AddHostRouteTo (iJK.GetAddress (1,0), ifInAddrB.GetLocal (), rvector ({3},{1}));//I->J(TCP)
  staticRoutingJ->AddHostRouteTo (Ipv4Address ("172.16.1.11"), iBD.GetAddress (0,0), rvector ({2},{1}));//J->K
  staticRoutingJ->AddHostRouteTo (Ipv4Address ("172.16.1.11"), iBC.GetAddress (0,0), rvector ({2},{1}));//J->K
  staticRoutingJ->AddHostRouteTo (Ipv4Address ("172.16.1.11"), ifInAddrB.GetLocal (), rvector ({2},{1}));//J->K(TCP)
  staticRoutingJ->AddHostRouteTo (iGK.GetAddress (1,0), ifInAddrB.GetLocal (), rvector ({2},{1}));//J->K(TCP)
  staticRoutingJ->AddHostRouteTo (iJK.GetAddress (1,0), ifInAddrB.GetLocal (), rvector ({2},{1}));//J->K(TCP)



  staticRoutingE->AddHostRouteTo (Ipv4Address ("172.16.1.4"), fromLocal, rvector ({1,2},{0.585,0.415}));//E->C,F(normal packet)
  staticRoutingE->AddHostRouteTo (Ipv4Address ("172.16.1.4"), iCE.GetAddress (1,0), rvector ({1,2},{0.585,0.415}));//E->C,F(TCP)
  staticRoutingE->AddHostRouteTo (Ipv4Address ("172.16.1.4"), iEF.GetAddress (0,0), rvector ({1,2},{0.585,0.415}));//E->C,F(TCP)
  staticRoutingE->AddHostRouteTo (Ipv4Address ("172.16.1.4"), ifInAddrE.GetLocal (), rvector ({1,2},{0.585,0.415}));//E->C,F(TCP)
  staticRoutingE->AddHostRouteTo (iBD.GetAddress (1,0), ifInAddrE.GetLocal (), rvector ({1,2},{0.585,0.415}));//E->C,F(TCP)
  staticRoutingE->AddHostRouteTo (iDF.GetAddress (0,0), ifInAddrE.GetLocal (), rvector ({1,2},{0.585,0.415}));//E->C,F(TCP)
  staticRoutingC->AddHostRouteTo (Ipv4Address ("172.16.1.4"), iCE.GetAddress (1,0), rvector ({2},{1}));//C->B
  staticRoutingC->AddHostRouteTo (Ipv4Address ("172.16.1.4"), iEF.GetAddress (0,0), rvector ({2},{1}));//C->B
  staticRoutingC->AddHostRouteTo (Ipv4Address ("172.16.1.4"), ifInAddrE.GetLocal (), rvector ({2},{1}));//C->B(TCP)
  staticRoutingC->AddHostRouteTo (iBD.GetAddress (1,0), ifInAddrE.GetLocal (), rvector ({2},{1}));//C->B(TCP)
  staticRoutingC->AddHostRouteTo (iDF.GetAddress (0,0), ifInAddrE.GetLocal (), rvector ({2},{1}));//C->B(TCP)
  staticRoutingB->AddHostRouteTo (Ipv4Address ("172.16.1.4"), iCE.GetAddress (1,0), rvector ({3},{1}));//B->D
  staticRoutingB->AddHostRouteTo (Ipv4Address ("172.16.1.4"), iEF.GetAddress (0,0), rvector ({3},{1}));//B->D
  staticRoutingB->AddHostRouteTo (Ipv4Address ("172.16.1.4"), ifInAddrE.GetLocal (), rvector ({3},{1}));//B->D(TCP)
  staticRoutingB->AddHostRouteTo (iBD.GetAddress (1,0), ifInAddrE.GetLocal (), rvector ({3},{1}));//B->D(TCP)
  staticRoutingB->AddHostRouteTo (iDF.GetAddress (0,0), ifInAddrE.GetLocal (), rvector ({3},{1}));//B->D(TCP)
  staticRoutingF->AddHostRouteTo (Ipv4Address ("172.16.1.4"), iEF.GetAddress (0,0), rvector ({2},{1}));//F->D
  staticRoutingF->AddHostRouteTo (Ipv4Address ("172.16.1.4"), iCE.GetAddress (1,0), rvector ({2},{1}));//F->D
  staticRoutingF->AddHostRouteTo (Ipv4Address ("172.16.1.4"), ifInAddrE.GetLocal (), rvector ({2},{1}));//F->D(TCP)
  staticRoutingF->AddHostRouteTo (iBD.GetAddress (1,0), ifInAddrE.GetLocal (), rvector ({2},{1}));//F->D(TCP)
  staticRoutingF->AddHostRouteTo (iDF.GetAddress (0,0), ifInAddrE.GetLocal (), rvector ({2},{1}));//F->D(TCP)



  staticRoutingG->AddHostRouteTo (Ipv4Address ("172.16.1.6"), fromLocal, rvector ({1,2},{0.385,0.615}));//G->H,K(normal packet)
  staticRoutingG->AddHostRouteTo (Ipv4Address ("172.16.1.6"), iGH.GetAddress (0,0), rvector ({1,2},{0.385,0.615}));//G->H,K(TCP)
  staticRoutingG->AddHostRouteTo (Ipv4Address ("172.16.1.6"), ifInAddrG.GetLocal (), rvector ({1,2},{0.385,0.615}));//G->H,K(TCP)
  staticRoutingG->AddHostRouteTo (iEF.GetAddress (1,0), ifInAddrG.GetLocal (), rvector ({1,2},{0.385,0.615}));//G->H,K(TCP)
  staticRoutingG->AddHostRouteTo (iDF.GetAddress (1,0), ifInAddrG.GetLocal (), rvector ({1,2},{0.385,0.615}));//G->H,K(TCP)
  staticRoutingG->AddHostRouteTo (iFI.GetAddress (0,0), ifInAddrG.GetLocal (), rvector ({1,2},{0.385,0.615}));//G->H,K(TCP)
  staticRoutingH->AddHostRouteTo (Ipv4Address ("172.16.1.6"), iGH.GetAddress (0,0), rvector ({3},{1}));//H->I
  staticRoutingH->AddHostRouteTo (Ipv4Address ("172.16.1.6"), ifInAddrG.GetLocal (), rvector ({3},{1}));//H->I(TCP)
  staticRoutingH->AddHostRouteTo (iEF.GetAddress (1,0), ifInAddrG.GetLocal (), rvector ({3},{1}));//H->I(TCP)
  staticRoutingH->AddHostRouteTo (iDF.GetAddress (1,0), ifInAddrG.GetLocal (), rvector ({3},{1}));//H->I(TCP)
  staticRoutingH->AddHostRouteTo (iFI.GetAddress (0,0), ifInAddrG.GetLocal (), rvector ({3},{1}));//H->I(TCP)
  staticRoutingI->AddHostRouteTo (Ipv4Address ("172.16.1.6"), iGH.GetAddress (0,0), rvector ({1},{1}));//I->F
  staticRoutingI->AddHostRouteTo (Ipv4Address ("172.16.1.6"), ifInAddrG.GetLocal (), rvector ({1},{1}));//I->F(TCP)
  staticRoutingI->AddHostRouteTo (iEF.GetAddress (1,0), ifInAddrG.GetLocal (), rvector ({1},{1}));//I->F(TCP)
  staticRoutingI->AddHostRouteTo (iDF.GetAddress (1,0), ifInAddrG.GetLocal (), rvector ({1},{1}));//I->F(TCP)
  staticRoutingI->AddHostRouteTo (iFI.GetAddress (0,0), ifInAddrG.GetLocal (), rvector ({1},{1}));//I->F(TCP)
  staticRoutingK->AddHostRouteTo (Ipv4Address ("172.16.1.6"), iGK.GetAddress (0,0), rvector ({2},{1}));//K->J
  staticRoutingK->AddHostRouteTo (Ipv4Address ("172.16.1.6"), ifInAddrG.GetLocal (), rvector ({2},{1}));//K->J(TCP)
  staticRoutingK->AddHostRouteTo (iEF.GetAddress (1,0), ifInAddrG.GetLocal (), rvector ({2},{1}));//K->J(TCP)
  staticRoutingK->AddHostRouteTo (iDF.GetAddress (1,0), ifInAddrG.GetLocal (), rvector ({2},{1}));//K->J(TCP)
  staticRoutingK->AddHostRouteTo (iFI.GetAddress (0,0), ifInAddrG.GetLocal (), rvector ({2},{1}));//K->J(TCP)
  staticRoutingJ->AddHostRouteTo (Ipv4Address ("172.16.1.6"), iGK.GetAddress (0,0), rvector ({1},{1}));//J->I
  staticRoutingJ->AddHostRouteTo (Ipv4Address ("172.16.1.6"), ifInAddrG.GetLocal (), rvector ({1},{1}));//J->I(TCP)
  staticRoutingJ->AddHostRouteTo (iEF.GetAddress (1,0), ifInAddrG.GetLocal (), rvector ({1},{1}));//J->I(TCP)
  staticRoutingJ->AddHostRouteTo (iDF.GetAddress (1,0), ifInAddrG.GetLocal (), rvector ({1},{1}));//J->I(TCP)
  staticRoutingJ->AddHostRouteTo (iFI.GetAddress (0,0), ifInAddrG.GetLocal (), rvector ({1},{1}));//J->I(TCP)



  staticRoutingH->AddHostRouteTo (Ipv4Address ("172.16.1.4"), fromLocal, rvector ({1,3},{0.608,0.392}));//H->E,I(normal packet)
  staticRoutingH->AddHostRouteTo (Ipv4Address ("172.16.1.4"), iEH.GetAddress (1,0), rvector ({1,3},{0.608,0.392}));//H->E,I(TCP)
  staticRoutingH->AddHostRouteTo (Ipv4Address ("172.16.1.4"), iHI.GetAddress (0,0), rvector ({1,3},{0.608,0.392}));//H->E,I(TCP)
  staticRoutingH->AddHostRouteTo (Ipv4Address ("172.16.1.4"), ifInAddrH.GetLocal (), rvector ({1,3},{0.608,0.392}));//H->E,I(TCP)
  staticRoutingH->AddHostRouteTo (iBD.GetAddress (1,0), ifInAddrH.GetLocal (), rvector ({1,3},{0.608,0.392}));//H->E,I(TCP)
  staticRoutingH->AddHostRouteTo (iDF.GetAddress (0,0), ifInAddrH.GetLocal (), rvector ({1,3},{0.608,0.392}));//H->E,I(TCP)
  staticRoutingE->AddHostRouteTo (Ipv4Address ("172.16.1.4"), iEH.GetAddress (1,0), rvector ({2},{1}));//E->F
  staticRoutingE->AddHostRouteTo (Ipv4Address ("172.16.1.4"), iHI.GetAddress (0,0), rvector ({2},{1}));//E->F
  staticRoutingE->AddHostRouteTo (Ipv4Address ("172.16.1.4"), ifInAddrH.GetLocal (), rvector ({2},{1}));//E->F(TCP)
  staticRoutingE->AddHostRouteTo (iBD.GetAddress (1,0), ifInAddrH.GetLocal (), rvector ({2},{1}));//E->F(TCP)
  staticRoutingE->AddHostRouteTo (iDF.GetAddress (0,0), ifInAddrH.GetLocal (), rvector ({2},{1}));//E->F(TCP)
  staticRoutingF->AddHostRouteTo (Ipv4Address ("172.16.1.4"), iEH.GetAddress (1,0), rvector ({2},{1}));//F->D
  staticRoutingF->AddHostRouteTo (Ipv4Address ("172.16.1.4"), iHI.GetAddress (0,0), rvector ({2},{1}));//F->D
  staticRoutingF->AddHostRouteTo (Ipv4Address ("172.16.1.4"), ifInAddrH.GetLocal (), rvector ({2},{1}));//F->D(TCP)
  staticRoutingF->AddHostRouteTo (iBD.GetAddress (1,0), ifInAddrH.GetLocal (), rvector ({2},{1}));//F->D(TCP)
  staticRoutingF->AddHostRouteTo (iDF.GetAddress (0,0), ifInAddrH.GetLocal (), rvector ({2},{1}));//F->D(TCP)
  staticRoutingI->AddHostRouteTo (Ipv4Address ("172.16.1.4"), iHI.GetAddress (0,0), rvector ({1},{1}));//I->F
  staticRoutingI->AddHostRouteTo (Ipv4Address ("172.16.1.4"), iEH.GetAddress (1,0), rvector ({1},{1}));//I->F
  staticRoutingI->AddHostRouteTo (Ipv4Address ("172.16.1.4"), ifInAddrH.GetLocal (), rvector ({1},{1}));//I->F(TCP)
  staticRoutingI->AddHostRouteTo (iBD.GetAddress (1,0), ifInAddrH.GetLocal (), rvector ({1},{1}));//I->F(TCP)
  staticRoutingI->AddHostRouteTo (iDF.GetAddress (0,0), ifInAddrH.GetLocal (), rvector ({1},{1}));//I->F(TCP)



  staticRoutingK->AddHostRouteTo (Ipv4Address ("172.16.1.1"), fromLocal, rvector ({1,2},{0.717,0.283}));//K->G,J(normal packet)
  staticRoutingK->AddHostRouteTo (Ipv4Address ("172.16.1.1"), iGK.GetAddress (1,0), rvector ({1,2},{0.717,0.283}));//K->G,J(TCP)
  staticRoutingK->AddHostRouteTo (Ipv4Address ("172.16.1.1"), iJK.GetAddress (1,0), rvector ({1,2},{0.717,0.283}));//K->G,J(TCP)
  staticRoutingK->AddHostRouteTo (Ipv4Address ("172.16.1.1"), ifInAddrK.GetLocal (), rvector ({1,2},{0.717,0.283}));//K->G,J(TCP)
  staticRoutingK->AddHostRouteTo (iAB.GetAddress (0,0), ifInAddrK.GetLocal (), rvector ({1,2},{0.717,0.283}));//K->G,J(TCP)
  staticRoutingK->AddHostRouteTo (iAC.GetAddress (0,0), ifInAddrK.GetLocal (), rvector ({1,2},{0.717,0.283}));//K->G,J(TCP)
  staticRoutingG->AddHostRouteTo (Ipv4Address ("172.16.1.1"), iGK.GetAddress (1,0), rvector ({1},{1}));//G->H
  staticRoutingG->AddHostRouteTo (Ipv4Address ("172.16.1.1"), iJK.GetAddress (1,0), rvector ({1},{1}));//G->H
  staticRoutingG->AddHostRouteTo (Ipv4Address ("172.16.1.1"), ifInAddrK.GetLocal (), rvector ({1},{1}));//G->H(TCP)
  staticRoutingG->AddHostRouteTo (iAB.GetAddress (0,0), ifInAddrK.GetLocal (), rvector ({1},{1}));//G->H(TCP)
  staticRoutingG->AddHostRouteTo (iAC.GetAddress (0,0), ifInAddrK.GetLocal (), rvector ({1},{1}));//G->H(TCP)
  staticRoutingH->AddHostRouteTo (Ipv4Address ("172.16.1.1"), iGK.GetAddress (1,0), rvector ({1},{1}));//H->E
  staticRoutingH->AddHostRouteTo (Ipv4Address ("172.16.1.1"), iJK.GetAddress (1,0), rvector ({1},{1}));//H->E
  staticRoutingH->AddHostRouteTo (Ipv4Address ("172.16.1.1"), ifInAddrK.GetLocal (), rvector ({1},{1}));//H->E(TCP)
  staticRoutingH->AddHostRouteTo (iAB.GetAddress (0,0), ifInAddrK.GetLocal (), rvector ({1},{1}));//H->E(TCP)
  staticRoutingH->AddHostRouteTo (iAC.GetAddress (0,0), ifInAddrK.GetLocal (), rvector ({1},{1}));//H->E(TCP)
  staticRoutingE->AddHostRouteTo (Ipv4Address ("172.16.1.1"), iGK.GetAddress (1,0), rvector ({1},{1}));//E->C
  staticRoutingE->AddHostRouteTo (Ipv4Address ("172.16.1.1"), iJK.GetAddress (1,0), rvector ({1},{1}));//E->C
  staticRoutingE->AddHostRouteTo (Ipv4Address ("172.16.1.1"), ifInAddrK.GetLocal (), rvector ({1},{1}));//E->C(TCP)
  staticRoutingE->AddHostRouteTo (iAB.GetAddress (0,0), ifInAddrK.GetLocal (), rvector ({1},{1}));//E->C(TCP)
  staticRoutingE->AddHostRouteTo (iAC.GetAddress (0,0), ifInAddrK.GetLocal (), rvector ({1},{1}));//E->C(TCP)
  staticRoutingC->AddHostRouteTo (Ipv4Address ("172.16.1.1"), iGK.GetAddress (1,0), rvector ({1},{1}));//C->A
  staticRoutingC->AddHostRouteTo (Ipv4Address ("172.16.1.1"), iJK.GetAddress (1,0), rvector ({1},{1}));//C->A
  staticRoutingC->AddHostRouteTo (Ipv4Address ("172.16.1.1"), ifInAddrK.GetLocal (), rvector ({1},{1}));//C->A(TCP)
  staticRoutingC->AddHostRouteTo (iAB.GetAddress (0,0), ifInAddrK.GetLocal (), rvector ({1},{1}));//C->A(TCP)
  staticRoutingC->AddHostRouteTo (iAC.GetAddress (0,0), ifInAddrK.GetLocal (), rvector ({1},{1}));//C->A(TCP)
  staticRoutingJ->AddHostRouteTo (Ipv4Address ("172.16.1.1"), iJK.GetAddress (1,0), rvector ({1},{1}));//J->I
  staticRoutingJ->AddHostRouteTo (Ipv4Address ("172.16.1.1"), iGK.GetAddress (1,0), rvector ({1},{1}));//J->I
  staticRoutingJ->AddHostRouteTo (Ipv4Address ("172.16.1.1"), ifInAddrK.GetLocal (), rvector ({1},{1}));//J->I(TCP)
  staticRoutingJ->AddHostRouteTo (iAB.GetAddress (0,0), ifInAddrK.GetLocal (), rvector ({1},{1}));//J->I(TCP)
  staticRoutingJ->AddHostRouteTo (iAC.GetAddress (0,0), ifInAddrK.GetLocal (), rvector ({1},{1}));//J->I(TCP)
  staticRoutingI->AddHostRouteTo (Ipv4Address ("172.16.1.1"), iJK.GetAddress (1,0), rvector ({1},{1}));//I->F
  staticRoutingI->AddHostRouteTo (Ipv4Address ("172.16.1.1"), iGK.GetAddress (1,0), rvector ({1},{1}));//I->F
  staticRoutingI->AddHostRouteTo (Ipv4Address ("172.16.1.1"), ifInAddrK.GetLocal (), rvector ({1},{1}));//I->F(TCP)
  staticRoutingI->AddHostRouteTo (iAB.GetAddress (0,0), ifInAddrK.GetLocal (), rvector ({1},{1}));//I->F(TCP)
  staticRoutingI->AddHostRouteTo (iAC.GetAddress (0,0), ifInAddrK.GetLocal (), rvector ({1},{1}));//I->F(TCP)
  staticRoutingF->AddHostRouteTo (Ipv4Address ("172.16.1.1"), iJK.GetAddress (1,0), rvector ({2},{1}));//F->D
  staticRoutingF->AddHostRouteTo (Ipv4Address ("172.16.1.1"), iGK.GetAddress (1,0), rvector ({2},{1}));//F->D
  staticRoutingF->AddHostRouteTo (Ipv4Address ("172.16.1.1"), ifInAddrK.GetLocal (), rvector ({2},{1}));//F->D(TCP)
  staticRoutingF->AddHostRouteTo (iAB.GetAddress (0,0), ifInAddrK.GetLocal (), rvector ({2},{1}));//F->D(TCP)
  staticRoutingF->AddHostRouteTo (iAC.GetAddress (0,0), ifInAddrK.GetLocal (), rvector ({2},{1}));//F->D(TCP)
  staticRoutingD->AddHostRouteTo (Ipv4Address ("172.16.1.1"), iJK.GetAddress (1,0), rvector ({1},{1}));//D->B
  staticRoutingD->AddHostRouteTo (Ipv4Address ("172.16.1.1"), iGK.GetAddress (1,0), rvector ({1},{1}));//D->B
  staticRoutingD->AddHostRouteTo (Ipv4Address ("172.16.1.1"), ifInAddrK.GetLocal (), rvector ({1},{1}));//D->B(TCP)
  staticRoutingD->AddHostRouteTo (iAB.GetAddress (0,0), ifInAddrK.GetLocal (), rvector ({1},{1}));//D->B(TCP)
  staticRoutingD->AddHostRouteTo (iAC.GetAddress (0,0), ifInAddrK.GetLocal (), rvector ({1},{1}));//D->B(TCP)
  staticRoutingB->AddHostRouteTo (Ipv4Address ("172.16.1.1"), iJK.GetAddress (1,0), rvector ({1},{1}));//B->A
  staticRoutingB->AddHostRouteTo (Ipv4Address ("172.16.1.1"), iGK.GetAddress (1,0), rvector ({1},{1}));//B->A
  staticRoutingB->AddHostRouteTo (Ipv4Address ("172.16.1.1"), ifInAddrK.GetLocal (), rvector ({1},{1}));//B->A(TCP)
  staticRoutingB->AddHostRouteTo (iAB.GetAddress (0,0), ifInAddrK.GetLocal (), rvector ({1},{1}));//B->A(TCP)
  staticRoutingB->AddHostRouteTo (iAC.GetAddress (0,0), ifInAddrK.GetLocal (), rvector ({1},{1}));//B->A(TCP)


/////////////////////////////////////

  // Setup sink App
    int sinkPort = 9;

    PacketSinkHelper packetSinkHelper ("ns3::TcpSocketFactory", InetSocketAddress (Ipv4Address::GetAny (), sinkPort));
    std::array<ApplicationContainer, 11> sinkApps;
    for(int i = 0; i <= 10; i++){
      sinkApps[i] = packetSinkHelper.Install (c.Get (i));
      sinkApps[i].Start (Seconds (0.));
      sinkApps[i].Stop (Seconds (END_TIME));
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
          Ptr<MyApp> app = CreateObject<MyApp> ();
          Ptr<Node> node = c.Get (i);
          Address sinkAddress = InetSocketAddress (sinkAddresses[j], sinkPort);
          if (i == 4 && j == 5)
          {
            app->Setup (tid, node ,sinkAddress, PACKET_SIZE, 300000, DataRate ("28Mbps"), "n" + std::to_string(i) + "-n" + std::to_string(j));
          } else {
            app->Setup (tid, node ,sinkAddress, PACKET_SIZE, NUM_PACKETS, DataRate (DEFAULT_SEND_RATE), "n" + std::to_string(i) + "-n" + std::to_string(j));
          }
          node->AddApplication (app);
          app->SetStartTime (Seconds (0));
          app->SetStopTime (Seconds (END_TIME));
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
    AnimationInterface anim ("./Data/static-route-default.xml");
  //Animation settings end

  Simulator::Stop (Seconds (END_TIME));
  Simulator::Run ();
  Simulator::Destroy ();
  return 0;
}