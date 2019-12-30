#include <iostream>
#include <fstream>
#include <string>
#include <cassert>
#include <math.h>
#include <array>

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include "ns3/flow-monitor-helper.h"
#include "ns3/ipv4-global-routing-helper.h"
#include "ns3/netanim-module.h"
#include "ns3/traffic-control-helper.h"


#define PACKET_SIZE 1300 //bytes 分割・統合されないサイズにする
#define SEGMENT_SIZE 1300 //bytes この大きさのデータがたまると送信される
#define ONE_DATUM 100 //パケットで1データ
#define DEFAULT_SEND_RATE "5Mbps"
#define BOTTLE_NECK_LINK_RATE "20Mbps"
#define OTHER_LINK_RATE "85Mbps"
#define NUM_PACKETS 30000
#define END_TIME 41 //Seconds
#define INTERVAL 20 //Seconds
// #define TXQUEUE "5p" //先にうまる
// #define TCQUEUE "5p" //TXが埋まると使われる
#define TCP_TYPE "ns3::TcpNewReno"


using namespace ns3;


NS_LOG_COMPONENT_DEFINE ("Internet2");


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
    uint32_t    m_packetLoss;
    uint64_t    m_targetRate;
    Ptr<OutputStreamWrapper> m_cwndStream;
    Ptr<OutputStreamWrapper> m_datarateStream;
    Ptr<OutputStreamWrapper> m_lossStream;

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
    m_packetLoss (0),
    m_targetRate (0),
    m_cwndStream (),
    m_datarateStream (),
    m_lossStream()
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
  // m_cwndStream = ascii.CreateFileStream ("./Data/"+m_name+".cwnd");
  // m_datarateStream = ascii.CreateFileStream ("./Data/"+m_name+".drate");
  // m_lossStream = ascii.CreateFileStream ("./Data/"+m_name+".loss");
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
    *m_datarateStream->GetStream () << Simulator::Now ().GetSeconds () << " " << m_dataRate.GetBitRate () << std::endl;
    *m_lossStream->GetStream () << Simulator::Now ().GetSeconds () << " " << lossRate << std::endl;
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
  }
}

void
MyApp::CountTCPTx (const Ptr<const Packet> packet, const TcpHeader &header, const Ptr<const TcpSocketBase> socket)
{
  if(packet->GetSize () > 0) 
  {
    ++m_tcpsent;
  }
}

std::array<uint64_t, 28> pktCountAry = {0};
std::array<uint64_t, 28> pktSizeCountAry = {0};


static void
linkPktCount (uint16_t linkn, Ptr< const Packet > packet)
{
  pktCountAry[linkn - 1] += 1;
  pktSizeCountAry[linkn -1] += packet->GetSize ();
}


std::array<uint64_t, 28> pktLossCountAry = {0};
static void
linkPktLossCount (uint16_t const linkn, Ptr<ns3::QueueDiscItem const> item)
{
  pktLossCountAry[linkn - 1] += 1;
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


int 
main (int argc, char *argv[])
{
  CommandLine cmd;
  bool enableFlowMonitor = false;
  cmd.AddValue ("EnableMonitor", "Enable Flow Monitor", enableFlowMonitor);
  cmd.Parse (argc, argv);

  // Set default tcp type
  Config::SetDefault ("ns3::TcpL4Protocol::SocketType", StringValue (TCP_TYPE));
  // Set default tcp segment size
  Config::SetDefault ("ns3::TcpSocket::SegmentSize", UintegerValue (SEGMENT_SIZE)); 


  // Create node container
    NodeContainer c;
    c.Create (11);
    NodeContainer n0n1 = NodeContainer (c.Get (0), c.Get (1));
    NodeContainer n0n3 = NodeContainer (c.Get (0), c.Get (3));
    NodeContainer n1n2 = NodeContainer (c.Get (1), c.Get (2));
    NodeContainer n1n3 = NodeContainer (c.Get (1), c.Get (3));
    NodeContainer n3n4 = NodeContainer (c.Get (3), c.Get (4));
    NodeContainer n2n5 = NodeContainer (c.Get (2), c.Get (5));
    NodeContainer n4n6 = NodeContainer (c.Get (4), c.Get (6));
    NodeContainer n5n8 = NodeContainer (c.Get (5), c.Get (8));
    NodeContainer n4n5 = NodeContainer (c.Get (4), c.Get (5));
    NodeContainer n6n8 = NodeContainer (c.Get (6), c.Get (8));
    NodeContainer n6n7 = NodeContainer (c.Get (6), c.Get (7));
    NodeContainer n8n9 = NodeContainer (c.Get (8), c.Get (9));
    NodeContainer n7n10 = NodeContainer (c.Get (7), c.Get (10));
    NodeContainer n9n10 = NodeContainer (c.Get (9), c.Get (10));
  // Create node container end

  InternetStackHelper st;
  st.Install (c);


  // Setup p2p devices
  PointToPointHelper p2p, p2p_nr;
  // p2p.SetDeviceAttribute ("DataRate", StringValue (OTHER_LINK_RATE));
  p2p.SetChannelAttribute ("Delay", StringValue ("1ms"));

  // Create p2p devices
    NetDeviceContainer d0d1 = p2p.Install (n0n1);
    NetDeviceContainer d0d3 = p2p.Install (n0n3);
    NetDeviceContainer d1d2 = p2p.Install (n1n2);
    NetDeviceContainer d1d3 = p2p.Install (n1n3);
    NetDeviceContainer d3d4 = p2p.Install (n3n4);
    NetDeviceContainer d2d5 = p2p.Install (n2n5);
    NetDeviceContainer d4d6 = p2p.Install (n4n6);
    NetDeviceContainer d5d8 = p2p.Install (n5n8);
    NetDeviceContainer d4d5 = p2p.Install (n4n5);
    NetDeviceContainer d6d8 = p2p.Install (n6n8);
    NetDeviceContainer d6d7 = p2p.Install (n6n7);
    NetDeviceContainer d8d9 = p2p.Install (n8n9);
    NetDeviceContainer d7d10 = p2p.Install (n7n10);
    NetDeviceContainer d9d10 = p2p.Install (n9n10);
  // Create p2p devices end

  // Set data rate n0->n1
  Config::Set("/NodeList/0/$ns3::Node/DeviceList/1/$ns3::PointToPointNetDevice/DataRate", DataRateValue (DataRate("30Mbps")));
  // Set data rate n0->n3
  Config::Set("/NodeList/0/$ns3::Node/DeviceList/1/$ns3::PointToPointNetDevice/DataRate", DataRateValue (DataRate("30Mbps")));
  // Set data rate n1->n0
  Config::Set("/NodeList/1/$ns3::Node/DeviceList/1/$ns3::PointToPointNetDevice/DataRate", DataRateValue (DataRate("30Mbps")));
  // Set data rate n1->n2
  Config::Set("/NodeList/1/$ns3::Node/DeviceList/2/$ns3::PointToPointNetDevice/DataRate", DataRateValue (DataRate("60Mbps")));
  // Set data rate n1->n3
  Config::Set("/NodeList/1/$ns3::Node/DeviceList/3/$ns3::PointToPointNetDevice/DataRate", DataRateValue (DataRate("30Mbps")));
  // Set data rate n2->n1
  Config::Set("/NodeList/2/$ns3::Node/DeviceList/1/$ns3::PointToPointNetDevice/DataRate", DataRateValue (DataRate("54Mbps")));
  // Set data rate n2->n5
  Config::Set("/NodeList/2/$ns3::Node/DeviceList/2/$ns3::PointToPointNetDevice/DataRate", DataRateValue (DataRate("84Mbps")));
  // Set data rate n3->n0
  Config::Set("/NodeList/3/$ns3::Node/DeviceList/1/$ns3::PointToPointNetDevice/DataRate", DataRateValue (DataRate("30Mbps")));
  // Set data rate n3->n1
  Config::Set("/NodeList/3/$ns3::Node/DeviceList/2/$ns3::PointToPointNetDevice/DataRate", DataRateValue (DataRate("36Mbps")));
  // Set data rate n3->n4
  Config::Set("/NodeList/3/$ns3::Node/DeviceList/3/$ns3::PointToPointNetDevice/DataRate", DataRateValue (DataRate("84Mbps")));
  // Set data rate n4->n3
  Config::Set("/NodeList/4/$ns3::Node/DeviceList/1/$ns3::PointToPointNetDevice/DataRate", DataRateValue (DataRate("90Mbps")));
  // Set data rate n4->n6
  Config::Set("/NodeList/4/$ns3::Node/DeviceList/2/$ns3::PointToPointNetDevice/DataRate", DataRateValue (DataRate("90Mbps")));
  // Set data rate n4->n5
  Config::Set("/NodeList/4/$ns3::Node/DeviceList/3/$ns3::PointToPointNetDevice/DataRate", DataRateValue (DataRate("42Mbps")));
  // Set data rate n5->n2
  Config::Set("/NodeList/5/$ns3::Node/DeviceList/1/$ns3::PointToPointNetDevice/DataRate", DataRateValue (DataRate("78Mbps")));
  // Set data rate n5->n8
  Config::Set("/NodeList/5/$ns3::Node/DeviceList/2/$ns3::PointToPointNetDevice/DataRate", DataRateValue (DataRate("90Mbps")));
  // Set data rate n5->n4
  Config::Set("/NodeList/5/$ns3::Node/DeviceList/3/$ns3::PointToPointNetDevice/DataRate", DataRateValue (DataRate("42Mbps")));
  // Set data rate n6->n4
  Config::Set("/NodeList/6/$ns3::Node/DeviceList/1/$ns3::PointToPointNetDevice/DataRate", DataRateValue (DataRate("96Mbps")));
  // Set data rate n6->n8
  Config::Set("/NodeList/6/$ns3::Node/DeviceList/2/$ns3::PointToPointNetDevice/DataRate", DataRateValue (DataRate("42Mbps")));
  // Set data rate n6->n7
  Config::Set("/NodeList/6/$ns3::Node/DeviceList/3/$ns3::PointToPointNetDevice/DataRate", DataRateValue (DataRate("72Mbps")));
  // Set data rate n7->n6
  Config::Set("/NodeList/7/$ns3::Node/DeviceList/1/$ns3::PointToPointNetDevice/DataRate", DataRateValue (DataRate("78Mbps")));
  // Set data rate n7->n10
  Config::Set("/NodeList/7/$ns3::Node/DeviceList/2/$ns3::PointToPointNetDevice/DataRate", DataRateValue (DataRate("36Mbps")));
  // Set data rate n8->n5
  Config::Set("/NodeList/8/$ns3::Node/DeviceList/1/$ns3::PointToPointNetDevice/DataRate", DataRateValue (DataRate("84Mbps")));
  // Set data rate n8->n6
  Config::Set("/NodeList/8/$ns3::Node/DeviceList/2/$ns3::PointToPointNetDevice/DataRate", DataRateValue (DataRate("42Mbps")));
  // Set data rate n8->n9
  Config::Set("/NodeList/8/$ns3::Node/DeviceList/3/$ns3::PointToPointNetDevice/DataRate", DataRateValue (DataRate("72Mbps")));
  // Set data rate n9->n8
  Config::Set("/NodeList/9/$ns3::Node/DeviceList/1/$ns3::PointToPointNetDevice/DataRate", DataRateValue (DataRate("66Mbps")));
  // Set data rate n9->n10
  Config::Set("/NodeList/9/$ns3::Node/DeviceList/2/$ns3::PointToPointNetDevice/DataRate", DataRateValue (DataRate("36Mbps")));
  // Set data rate n10->n7
  Config::Set("/NodeList/10/$ns3::Node/DeviceList/1/$ns3::PointToPointNetDevice/DataRate", DataRateValue (DataRate("42Mbps")));
  // Set data rate n10->n9
  Config::Set("/NodeList/10/$ns3::Node/DeviceList/2/$ns3::PointToPointNetDevice/DataRate", DataRateValue (DataRate("30Mbps")));

  // Setup traffic control queue
    TrafficControlHelper tch_lim, tch;
    tch.SetRootQueueDisc ("ns3::FqCoDelQueueDisc");
    tch.Install (d0d1);
    tch.Install (d0d3);
    tch.Install (d1d2);
    tch.Install (d1d3);
    tch.Install (d3d4);
    tch.Install (d2d5);
    tch.Install (d4d6);
    tch.Install (d5d8);
    tch.Install (d6d8);
    tch.Install (d6d7);
    tch.Install (d8d9);
    tch.Install (d7d10);
    tch.Install (d9d10);
    tch.Install (d4d5);
    // tch_lim.SetRootQueueDisc ("ns3::FqCoDelQueueDisc", "MaxSize", QueueSizeValue (QueueSize (TCQUEUE)));
    // tch_lim.Install (d0d1);
  // Setup traffic control queue end


  // Set queue size
  // PointerValue queue;
  // d0d1.Get (0)->GetAttribute ("TxQueue", queue);
  // Ptr<Queue<Packet>> txQueueD0 = queue.Get<Queue<Packet>> ();
  // txQueueD0->SetAttribute("MaxSize", StringValue (TXQUEUE));


  // Link traffic monitor reg
    //link0->1
    d0d1.Get (0)->TraceConnectWithoutContext("PhyTxEnd", MakeBoundCallback(&linkPktCount, 1));
    //link0->3
    d0d3.Get (0)->TraceConnectWithoutContext("PhyTxEnd", MakeBoundCallback(&linkPktCount, 2));
    //link1->0
    d0d1.Get (1)->TraceConnectWithoutContext("PhyTxEnd", MakeBoundCallback(&linkPktCount, 3));
    //link1->2
    d1d2.Get (0)->TraceConnectWithoutContext("PhyTxEnd", MakeBoundCallback(&linkPktCount, 4));
    //link1->3
    d1d3.Get (0)->TraceConnectWithoutContext("PhyTxEnd", MakeBoundCallback(&linkPktCount, 5));  
    //link2->1
    d1d2.Get (1)->TraceConnectWithoutContext("PhyTxEnd", MakeBoundCallback(&linkPktCount, 6));
    //link2->5
    d2d5.Get (0)->TraceConnectWithoutContext("PhyTxEnd", MakeBoundCallback(&linkPktCount, 7));
    //link3->0
    d0d3.Get (1)->TraceConnectWithoutContext("PhyTxEnd", MakeBoundCallback(&linkPktCount, 8));
    //link3->1
    d1d3.Get (1)->TraceConnectWithoutContext("PhyTxEnd", MakeBoundCallback(&linkPktCount, 9));  
    //link3->4
    d3d4.Get (0)->TraceConnectWithoutContext("PhyTxEnd", MakeBoundCallback(&linkPktCount, 10));
    //link4->3
    d3d4.Get (1)->TraceConnectWithoutContext("PhyTxEnd", MakeBoundCallback(&linkPktCount, 11));
    //link4->6
    d4d6.Get (0)->TraceConnectWithoutContext("PhyTxEnd", MakeBoundCallback(&linkPktCount, 12));
    //link4->5
    d4d5.Get (0)->TraceConnectWithoutContext("PhyTxEnd", MakeBoundCallback(&linkPktCount, 13));
    //link5->2
    d2d5.Get (1)->TraceConnectWithoutContext("PhyTxEnd", MakeBoundCallback(&linkPktCount, 14));
    //link5->8
    d5d8.Get (0)->TraceConnectWithoutContext("PhyTxEnd", MakeBoundCallback(&linkPktCount, 15));
    //link5->4
    d4d5.Get (1)->TraceConnectWithoutContext("PhyTxEnd", MakeBoundCallback(&linkPktCount, 16));
    //link6->4
    d4d6.Get (1)->TraceConnectWithoutContext("PhyTxEnd", MakeBoundCallback(&linkPktCount, 17));
    //link6->8
    d6d8.Get (0)->TraceConnectWithoutContext("PhyTxEnd", MakeBoundCallback(&linkPktCount, 18));
    //link6->7
    d6d7.Get (0)->TraceConnectWithoutContext("PhyTxEnd", MakeBoundCallback(&linkPktCount, 19));
    //link7->6
    d6d7.Get (1)->TraceConnectWithoutContext("PhyTxEnd", MakeBoundCallback(&linkPktCount, 20));
    //link7->10
    d7d10.Get (0)->TraceConnectWithoutContext("PhyTxEnd", MakeBoundCallback(&linkPktCount, 21));
    //link8->5
    d5d8.Get (1)->TraceConnectWithoutContext("PhyTxEnd", MakeBoundCallback(&linkPktCount, 22));
    //link8->6
    d6d8.Get (1)->TraceConnectWithoutContext("PhyTxEnd", MakeBoundCallback(&linkPktCount, 23));
    //link8->9
    d8d9.Get (0)->TraceConnectWithoutContext("PhyTxEnd", MakeBoundCallback(&linkPktCount, 24));
    //link9->8
    d8d9.Get (1)->TraceConnectWithoutContext("PhyTxEnd", MakeBoundCallback(&linkPktCount, 25));
    //link9->10
    d9d10.Get (0)->TraceConnectWithoutContext("PhyTxEnd", MakeBoundCallback(&linkPktCount, 26));
    //link10->7
    d7d10.Get (1)->TraceConnectWithoutContext("PhyTxEnd", MakeBoundCallback(&linkPktCount, 27));
    //link10->9
    d9d10.Get (1)->TraceConnectWithoutContext("PhyTxEnd", MakeBoundCallback(&linkPktCount, 28));
  // Link flow monitor reg end


  // Packet loss monitor reg

    //n0->n1 1
    Config::ConnectWithoutContext ("/NodeList/0/$ns3::TrafficControlLayer/RootQueueDiscList/1/Drop", MakeBoundCallback (&linkPktLossCount, 1));
    //n0->n3 2
    Config::ConnectWithoutContext ("/NodeList/0/$ns3::TrafficControlLayer/RootQueueDiscList/2/Drop", MakeBoundCallback (&linkPktLossCount, 2));
    //n1->n0 3
    Config::ConnectWithoutContext ("/NodeList/1/$ns3::TrafficControlLayer/RootQueueDiscList/1/Drop", MakeBoundCallback (&linkPktLossCount, 3));
    //n1->n2 4
    Config::ConnectWithoutContext ("/NodeList/1/$ns3::TrafficControlLayer/RootQueueDiscList/2/Drop", MakeBoundCallback (&linkPktLossCount, 4));
    //n1->n3 5
    Config::ConnectWithoutContext ("/NodeList/1/$ns3::TrafficControlLayer/RootQueueDiscList/3/Drop", MakeBoundCallback (&linkPktLossCount, 5));
    //n2->n1 6
    Config::ConnectWithoutContext ("/NodeList/2/$ns3::TrafficControlLayer/RootQueueDiscList/1/Drop", MakeBoundCallback (&linkPktLossCount, 6));
    //n2->n5 7
    Config::ConnectWithoutContext ("/NodeList/2/$ns3::TrafficControlLayer/RootQueueDiscList/2/Drop", MakeBoundCallback (&linkPktLossCount, 7));
    //n3->n0 8
    Config::ConnectWithoutContext ("/NodeList/3/$ns3::TrafficControlLayer/RootQueueDiscList/1/Drop", MakeBoundCallback (&linkPktLossCount, 8));
    //n3->n1 9
    Config::ConnectWithoutContext ("/NodeList/3/$ns3::TrafficControlLayer/RootQueueDiscList/2/Drop", MakeBoundCallback (&linkPktLossCount, 9));
    //n3->n4 10
    Config::ConnectWithoutContext ("/NodeList/3/$ns3::TrafficControlLayer/RootQueueDiscList/3/Drop", MakeBoundCallback (&linkPktLossCount, 10));
    //n4->n3 11
    Config::ConnectWithoutContext ("/NodeList/4/$ns3::TrafficControlLayer/RootQueueDiscList/1/Drop", MakeBoundCallback (&linkPktLossCount, 11));
    //n4->n6 12
    Config::ConnectWithoutContext ("/NodeList/4/$ns3::TrafficControlLayer/RootQueueDiscList/2/Drop", MakeBoundCallback (&linkPktLossCount, 12));
    //n4->n5 13
    Config::ConnectWithoutContext ("/NodeList/4/$ns3::TrafficControlLayer/RootQueueDiscList/3/Drop", MakeBoundCallback (&linkPktLossCount, 13));
    //n5->n2 14
    Config::ConnectWithoutContext ("/NodeList/5/$ns3::TrafficControlLayer/RootQueueDiscList/1/Drop", MakeBoundCallback (&linkPktLossCount, 14));
    //n5->n8 15
    Config::ConnectWithoutContext ("/NodeList/5/$ns3::TrafficControlLayer/RootQueueDiscList/2/Drop", MakeBoundCallback (&linkPktLossCount, 15));
    //n5->n4 16
    Config::ConnectWithoutContext ("/NodeList/5/$ns3::TrafficControlLayer/RootQueueDiscList/3/Drop", MakeBoundCallback (&linkPktLossCount, 16));
    //n6->n4 17
    Config::ConnectWithoutContext ("/NodeList/6/$ns3::TrafficControlLayer/RootQueueDiscList/1/Drop", MakeBoundCallback (&linkPktLossCount, 17));
    //n6->n8 18
    Config::ConnectWithoutContext ("/NodeList/6/$ns3::TrafficControlLayer/RootQueueDiscList/2/Drop", MakeBoundCallback (&linkPktLossCount, 18));
    //n6->n7 19
    Config::ConnectWithoutContext ("/NodeList/6/$ns3::TrafficControlLayer/RootQueueDiscList/3/Drop", MakeBoundCallback (&linkPktLossCount, 19));
    //n7->n6 20
    Config::ConnectWithoutContext ("/NodeList/7/$ns3::TrafficControlLayer/RootQueueDiscList/1/Drop", MakeBoundCallback (&linkPktLossCount, 20));
    //n7->n10 21
    Config::ConnectWithoutContext ("/NodeList/7/$ns3::TrafficControlLayer/RootQueueDiscList/2/Drop", MakeBoundCallback (&linkPktLossCount, 21));
    //n8->n5 22
    Config::ConnectWithoutContext ("/NodeList/8/$ns3::TrafficControlLayer/RootQueueDiscList/1/Drop", MakeBoundCallback (&linkPktLossCount, 22));
    //n8->n6 23
    Config::ConnectWithoutContext ("/NodeList/8/$ns3::TrafficControlLayer/RootQueueDiscList/2/Drop", MakeBoundCallback (&linkPktLossCount, 23));
    //n8->n9 24
    Config::ConnectWithoutContext ("/NodeList/8/$ns3::TrafficControlLayer/RootQueueDiscList/3/Drop", MakeBoundCallback (&linkPktLossCount, 24));
    //n9->n8 25
    Config::ConnectWithoutContext ("/NodeList/9/$ns3::TrafficControlLayer/RootQueueDiscList/1/Drop", MakeBoundCallback (&linkPktLossCount, 25));
    //n9->n10 26
    Config::ConnectWithoutContext ("/NodeList/9/$ns3::TrafficControlLayer/RootQueueDiscList/2/Drop", MakeBoundCallback (&linkPktLossCount, 26));
    //n10->n7 27
    Config::ConnectWithoutContext ("/NodeList/10/$ns3::TrafficControlLayer/RootQueueDiscList/1/Drop", MakeBoundCallback (&linkPktLossCount, 27));
    //n10->n9 28
    Config::ConnectWithoutContext ("/NodeList/10/$ns3::TrafficControlLayer/RootQueueDiscList/2/Drop", MakeBoundCallback (&linkPktLossCount, 28));
  // Packet loss monitor reg end


  // Assign ip address
      Ipv4AddressHelper ipv4;
      ipv4.SetBase ("10.1.1.0", "255.255.255.0"); Ipv4InterfaceContainer i0i1 = ipv4.Assign (d0d1);
      ipv4.SetBase ("10.1.2.0", "255.255.255.0"); Ipv4InterfaceContainer i0i3 = ipv4.Assign (d0d3);
      ipv4.SetBase ("10.1.3.0", "255.255.255.0"); Ipv4InterfaceContainer i1i2 = ipv4.Assign (d1d2);
      ipv4.SetBase ("10.1.4.0", "255.255.255.0"); Ipv4InterfaceContainer i1i3 = ipv4.Assign (d1d3);
      ipv4.SetBase ("10.1.5.0", "255.255.255.0"); Ipv4InterfaceContainer i3i4 = ipv4.Assign (d3d4);
      ipv4.SetBase ("10.1.6.0", "255.255.255.0"); Ipv4InterfaceContainer i2i5 = ipv4.Assign (d2d5);
      ipv4.SetBase ("10.1.7.0", "255.255.255.0"); Ipv4InterfaceContainer i4i6 = ipv4.Assign (d4d6);
      ipv4.SetBase ("10.1.8.0", "255.255.255.0"); Ipv4InterfaceContainer i5i8 = ipv4.Assign (d5d8);
      ipv4.SetBase ("10.1.9.0", "255.255.255.0"); Ipv4InterfaceContainer i4i5 = ipv4.Assign (d4d5);
      ipv4.SetBase ("10.1.10.0", "255.255.255.0"); Ipv4InterfaceContainer i6i8 = ipv4.Assign (d6d8);
      ipv4.SetBase ("10.1.11.0", "255.255.255.0"); Ipv4InterfaceContainer i6i7 = ipv4.Assign (d6d7);
      ipv4.SetBase ("10.1.12.0", "255.255.255.0"); Ipv4InterfaceContainer i8i9 = ipv4.Assign (d8d9);
      ipv4.SetBase ("10.1.13.0", "255.255.255.0"); Ipv4InterfaceContainer i7i10 = ipv4.Assign (d7d10);
      ipv4.SetBase ("10.1.14.0", "255.255.255.0"); Ipv4InterfaceContainer i9i10 = ipv4.Assign (d9d10);
  // Assign ip address end


  Ipv4GlobalRoutingHelper::PopulateRoutingTables ();


  // Setup sink applications
    uint16_t sinkPort = 9;

    Address sAddrs0 (InetSocketAddress (i0i1.GetAddress (0), sinkPort));
    Address sAddrs1 (InetSocketAddress (i0i1.GetAddress (1), sinkPort));
    Address sAddrs2 (InetSocketAddress (i1i2.GetAddress (1), sinkPort));
    Address sAddrs3 (InetSocketAddress (i0i3.GetAddress (1), sinkPort));
    Address sAddrs4 (InetSocketAddress (i3i4.GetAddress (1), sinkPort));
    Address sAddrs5 (InetSocketAddress (i2i5.GetAddress (1), sinkPort));
    Address sAddrs6 (InetSocketAddress (i4i6.GetAddress (1), sinkPort));
    Address sAddrs7 (InetSocketAddress (i6i7.GetAddress (1), sinkPort));
    Address sAddrs8 (InetSocketAddress (i5i8.GetAddress (1), sinkPort));
    Address sAddrs9 (InetSocketAddress (i8i9.GetAddress (1), sinkPort));
    Address sAddrs10 (InetSocketAddress (i7i10.GetAddress (1), sinkPort));
    std::array<Address, 11> addresses = {
        sAddrs0, sAddrs1, sAddrs2, sAddrs3, sAddrs4,
        sAddrs5, sAddrs6, sAddrs7, sAddrs8, sAddrs9, sAddrs10
        };

    PacketSinkHelper packetSinkHelper ("ns3::TcpSocketFactory", InetSocketAddress (Ipv4Address::GetAny (), sinkPort));

    std::array<ApplicationContainer,11> sinkApps;

    for(int i = 0; i <= 10; i++){
      sinkApps[i] = packetSinkHelper.Install (c.Get (i));
      sinkApps[i].Start (Seconds (0.));
      sinkApps[i].Stop (Seconds (END_TIME));
    }
  // Setup sink applications end
 

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
            Address sinkAddress = addresses[j];
            app->Setup (tid, node ,sinkAddress, PACKET_SIZE, NUM_PACKETS, DataRate (DEFAULT_SEND_RATE), "n" + std::to_string(i) + "-n" + std::to_string(j));
            node->AddApplication (app);
            app->SetStartTime (Seconds (0));
            app->SetStopTime (Seconds (END_TIME - 1));
          }
        }
      }
  // Setup source applications end


  // Trace settings
  AsciiTraceHelper ascii;
  // p2p_nr.EnableAsciiAll (ascii.CreateFileStream ("./Data/internet2.tr"));
  streamLinkTrafSize = ascii.CreateFileStream ("./matrix/link.traf");
  streamLinkPktCount = ascii.CreateFileStream ("./matrix/link.pktc");
  streamLinkLossCount = ascii.CreateFileStream ("./matrix/link.loss");

  Simulator::Schedule(Time (Seconds (INTERVAL)), &monitorLink, INTERVAL);
  *streamLinkTrafSize->GetStream ()<< INTERVAL <<"\n\n";
  *streamLinkTrafSize->GetStream ()<< END_TIME / INTERVAL <<"\n\n";

  // Flow Monitor
  FlowMonitorHelper flowmonHelper;
  if (enableFlowMonitor)
  {
    flowmonHelper.InstallAll ();
  }


  // Animation settings
    // AnimationInterface::SetConstantPosition (c.Get (0),2.0,2.0);
    // AnimationInterface::SetConstantPosition (c.Get (1),2.0,4.0);
    // AnimationInterface::SetConstantPosition (c.Get (2),3.0,6.0);
    // AnimationInterface::SetConstantPosition (c.Get (3),4.0,4.0);
    // AnimationInterface::SetConstantPosition (c.Get (4),6.0,4.0);
    // AnimationInterface::SetConstantPosition (c.Get (5),6.0,6.0);
    // AnimationInterface::SetConstantPosition (c.Get (6),8.0,4.0);
    // AnimationInterface::SetConstantPosition (c.Get (7),8.0,3.0);
    // AnimationInterface::SetConstantPosition (c.Get (8),8.0,6.0);
    // AnimationInterface::SetConstantPosition (c.Get (9),9.0,5.0);
    // AnimationInterface::SetConstantPosition (c.Get (10),10.0,4.0);
    // AnimationInterface anim ("./Data/internet2.xml");
  //Animation settings end
  
  
  Simulator::Stop (Seconds (END_TIME));
  Simulator::Run ();


  if (enableFlowMonitor)
  {
    flowmonHelper.SerializeToXmlFile ("./Data/internet2.flowmon", false, false);
  }
  

  Simulator::Destroy ();
  return 0;
}