#include <iostream>
#include <fstream>
#include <string>
#include <cassert>
#include <math.h>

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include "ns3/flow-monitor-helper.h"
#include "ns3/ipv4-global-routing-helper.h"
#include "ns3/netanim-module.h"
#include "ns3/traffic-control-helper.h"
#include "ns3/config-store.h"

#include "Eigen/Core"
#include "Eigen/LU"

#define PACKET_SIZE 1300 //bytes 分割・統合されないサイズにする
#define SEGMENT_SIZE 1300 //bytes この大きさのデータがたまると送信される
#define ONE_DATUM 100 //パケットで1データ
#define DEFAULT_SEND_RATE "2Mbps"
#define MONITOR_LINK_RATE "5Mbps"
#define OTHER_LINK_RATE "10Mbps"
#define NUM_PACKETS 20000
#define END_TIME 150 //Seconds
#define TXQUEUE "5p" //先にうまる
#define TCQUEUE "5p" //TXが埋まると使われる
#define TCP_TYPE "ns3::TcpNewReno"


using namespace ns3;

using namespace Eigen;


NS_LOG_COMPONENT_DEFINE ("Simple");


Ptr<OutputStreamWrapper> streamLinkUtil;
Ptr<OutputStreamWrapper> streamLinkLoss;


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
    double      m_previousLossRate;
    uint64_t    m_targetRate;
    Ptr<OutputStreamWrapper> m_cwndStream;
    Ptr<OutputStreamWrapper> m_datarateStream;
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
    m_previousLossRate (0),
    m_targetRate (0),
    m_cwndStream (),
    m_datarateStream ()
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
  m_previousLossRate = 0;

  AsciiTraceHelper ascii;
  m_cwndStream = ascii.CreateFileStream ("./Data/"+m_name+".cwnd");
  m_datarateStream = ascii.CreateFileStream ("./Data/"+m_name+".drate");
}

void
MyApp::StartApplication (void)
{
  m_running = true;
  m_packetsSent = 0;
  m_socket->Bind ();
  m_socket->Connect (m_peer);
  m_socket->ShutdownRecv ();
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
  m_previousLossRate = m_packetLoss / (double) m_tcpsent;
  m_packetLoss = 0;
  m_tcpsent = 0;
  m_running = true;
  m_socket = Socket::CreateSocket (m_node, m_tid);;
  m_socket->Bind ();
  m_socket->Connect (m_peer);
  m_socket->ShutdownRecv ();
  m_socket->TraceConnectWithoutContext("Tx", MakeCallback (&MyApp::CountTCPTx, this));
  m_socket->TraceConnectWithoutContext("CongestionWindow", MakeCallback (&MyApp::DetectPacketLoss, this));
  SendPacket ();
}

void
MyApp::SendPacket (void)
{
  Ptr<Packet> packet = Create<Packet> (m_packetSize);
  m_socket->Send (packet);
  // １データ送信でコネクション終了
  if(++m_packetsSent % ONE_DATUM == 0)
  {
    StopApplication ();
    ChangeDataRate (m_packetLoss / (double) m_tcpsent);
    Simulator::ScheduleNow (&MyApp::ReConnect,this);
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
  uint64_t dataRateNow = m_dataRate.GetBitRate ();
  if (m_previousLossRate < 0.001 && dataRateNow < m_targetRate)
  {
    m_dataRate = DataRate(m_targetRate * (1 / exp(-11 * lossRate)));
  }else{
    m_dataRate =  DataRate(static_cast<uint64_t>(dataRateNow * exp (-11 * lossRate)));
  }
  *m_datarateStream->GetStream () << Simulator::Now ().GetSeconds () << " " << m_dataRate.GetBitRate () << std::endl;
}

void
MyApp::DetectPacketLoss (const uint32_t org, const uint32_t cgd)
{
  *m_cwndStream->GetStream () << Simulator::Now ().GetSeconds () << " " << cgd << std::endl;
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

// variable for packet count
//Tx
uint64_t d1tx_t = 0; uint64_t d2tx_t = 0; uint64_t d3tx_t = 0; uint64_t d4tx_t = 0;
//Rx
uint64_t d1rx_t = 0; uint64_t d2rx_t = 0; uint64_t d3rx_t = 0; uint64_t d4rx_t = 0;

uint64_t losspacket = 0;

static void
linkPktCount (uint16_t linkn, Ptr< const Packet > packet)
{
  switch (linkn)
  {
  case 1:
    d1tx_t += packet->GetSize (); break;
  case 2:
    d2tx_t += packet->GetSize (); break;
  case 3:
    d3tx_t += packet->GetSize (); break;
  case 4:
    d4tx_t += packet->GetSize (); break;
  case 5:
    d1rx_t += packet->GetSize (); break;
  case 6:
    d2rx_t += packet->GetSize (); break;
  case 7:
    d3rx_t += packet->GetSize (); break;
  case 8:
    d4rx_t += packet->GetSize (); break;
  default:
    break;
  }
}


static void
monitorLink (double time, uint64_t maxdata)
{
  Matrix<double, 4, 4> route;
  route << 1,0,0,0,
           1,1,0,0,
           1,1,1,0,
           1,1,1,1;

  Matrix<double, 4, 4> rinv;
  rinv << 1,0,0,0,
         -1,1,0,0,
         0,-1,1,0,
         0,0,-1,1;

  *streamLinkUtil->GetStream () << Simulator::Now ().GetSeconds () << " " << (double) d4tx_t * 8 / (maxdata * time) << std::endl;
  
  double loss45 = 0;
  if ((1 - ((double) d4tx_t / d4rx_t)) < 0 ){
    loss45 = 0;
  } else {
    loss45 = 1 - ((double) d4tx_t / d4rx_t);
    // *streamLinkLoss->GetStream () << Simulator::Now ().GetSeconds () << " " << loss45 << std::endl;
  }


  Matrix<double,4,1> flow;
  flow << d1tx_t * 8 / time / 1000000, d2tx_t * 8 / time / 1000000, d3tx_t * 8 / time / 1000000, d4tx_t * 8 / time / 1000000;
  Matrix<double,4,1> odflow;
  odflow = rinv * flow;

  std::cout << "flow : " << flow << std::endl;
  
  //Link packet loss
  double loss12 = 1 - (double)d1tx_t / d1rx_t;
  double loss23 = 1 - (double)d2tx_t / d2rx_t;
  double loss34 = 1 - (double)d3tx_t / d3rx_t;

  //OD packet loss
  double loss15 = 1 - ((1 - loss12) * (1 - loss23) * (1 - loss34) * (1 - loss45));
  double loss25 = 1 - ((1 - loss23) * (1 - loss34) * (1 - loss45));
  double loss35 = 1 - ((1 - loss34) * (1 - loss45));

  //OD latent traffic
  double latent15 =  (1 / exp (-11 * loss15)) * odflow.coeff (0,0);
  double latent25 =  (1 / exp (-11 * loss25)) * odflow.coeff (1,0);
  double latent35 =  (1 / exp (-11 * loss35)) * odflow.coeff (2,0);
  double latent45 =  (1 / exp (-11 * loss45)) * odflow.coeff (3,0);

  Matrix<double,4,1> latent;
  latent << latent15,latent25,latent35,latent45;

  std::cout << "latent traffic 1->5 : " << latent15 << "Mbit" << std::endl;
  std::cout << "latent traffic 2->5 : " << latent25 << "Mbit" << std::endl;
  std::cout << "latent traffic 3->5 : " << latent35 << "Mbit" << std::endl;
  std::cout << "latent traffic 4->5 : " << latent45 << "Mbit\n" << std::endl;
  
  //Link latent traffic
  std::cout << route * latent << "\n" <<std::endl;

  std::cout << "d4tx        : " << d4tx_t * 8 / time / 1000000 <<std::endl;
  std::cout << "d4rx - loss : " << (d4rx_t - losspacket) * 8 / time / 1000000 <<std::endl;
  std::cout << "d4rx : " << d4rx_t * 8 / time / 1000000 <<std::endl;

  d1tx_t = 0; d2tx_t = 0; d3tx_t = 0; d4tx_t = 0;
  d1rx_t = 0; d2rx_t = 0; d3rx_t = 0; d4rx_t = 0;
  losspacket = 0;
  Simulator::Schedule (Time ( Seconds (time)), &monitorLink, time, maxdata);
}

static void
testcb(Ptr<ns3::QueueDiscItem const> itm, char const *c)
{
//  std::cout << "drop " << c <<std::endl;
 losspacket += itm->GetPacket ()->GetSize ();
}

static void
test(Ptr<ns3::QueueDiscItem const> itm)
{
//  std::cout << "drop " <<std::endl;
losspacket += itm->GetPacket ()->GetSize ();

}

int 
main (int argc, char *argv[])
{
  CommandLine cmd;
  bool enableFlowMonitor = false;
  cmd.AddValue ("EnableMonitor", "Enable Flow Monitor", enableFlowMonitor);
  cmd.Parse (argc, argv);

  ConfigStore config;
  config.ConfigureDefaults ();

  //Set default tcp type
  Config::SetDefault ("ns3::TcpL4Protocol::SocketType", StringValue (TCP_TYPE));
  //Set default tcp segment size
  Config::SetDefault ("ns3::TcpSocket::SegmentSize", UintegerValue (SEGMENT_SIZE)); 


  NodeContainer c;
  NodeContainer ca;

  c.Create (9);
  NodeContainer n0n1 = NodeContainer (c.Get (0), c.Get (1));
  NodeContainer n0n8 = NodeContainer (c.Get (0), c.Get (8));
  NodeContainer n1n2 = NodeContainer (c.Get (1), c.Get (2));
  NodeContainer n1n8 = NodeContainer (c.Get (1), c.Get (8));
  NodeContainer n2n3 = NodeContainer (c.Get (2), c.Get (3));
  NodeContainer n2n7 = NodeContainer (c.Get (2), c.Get (7));
  NodeContainer n3n4 = NodeContainer (c.Get (3), c.Get (4));
  NodeContainer n3n6 = NodeContainer (c.Get (3), c.Get (6));
  NodeContainer n4n5 = NodeContainer (c.Get (4), c.Get (5));
  NodeContainer n4n6 = NodeContainer (c.Get (4), c.Get (6));
  NodeContainer n5n6 = NodeContainer (c.Get (5), c.Get (6));
  NodeContainer n6n7 = NodeContainer (c.Get (6), c.Get (7));
  NodeContainer n7n8 = NodeContainer (c.Get (7), c.Get (8));

  ca.Create(3);
  NodeContainer a0c1 = NodeContainer (ca.Get (0), c.Get (1));
  NodeContainer a1c2 = NodeContainer (ca.Get (1), c.Get (2));
  NodeContainer a2c3 = NodeContainer (ca.Get (2), c.Get (3));


  InternetStackHelper st;
  st.Install (c);
  st.Install (ca);

  // Setup for p2p devices
  PointToPointHelper p2p, p2p_nr;
  p2p.SetDeviceAttribute ("DataRate", StringValue (OTHER_LINK_RATE));
  p2p.SetChannelAttribute ("Delay", StringValue ("1ms"));
  p2p_nr.SetDeviceAttribute ("DataRate", StringValue (MONITOR_LINK_RATE));
  p2p_nr.SetChannelAttribute ("Delay", StringValue ("1ms"));

  // Create p2p devices
  NetDeviceContainer d0d1 = p2p.Install (n0n1);
  NetDeviceContainer d0d8 = p2p.Install (n0n8);
  NetDeviceContainer d1d2 = p2p.Install (n1n2);
  NetDeviceContainer d1d8 = p2p.Install (n1n8);
  NetDeviceContainer d2d3 = p2p.Install (n2n3);
  NetDeviceContainer d2d7 = p2p.Install (n2n7);
  NetDeviceContainer d3d4 = p2p.Install (n3n4);
  NetDeviceContainer d3d6 = p2p.Install (n3n6);
  NetDeviceContainer d4d6 = p2p.Install (n4n6);
  NetDeviceContainer d5d6 = p2p.Install (n5n6);
  NetDeviceContainer d7d8 = p2p.Install (n7n8);
  NetDeviceContainer d4d5 = p2p_nr.Install (n4n5);
  NetDeviceContainer d6d7 = p2p.Install (n6n7);

  NetDeviceContainer da0c1 = p2p.Install (a0c1);
  NetDeviceContainer da1c2 = p2p.Install (a1c2);
  NetDeviceContainer da2c3 = p2p.Install (a2c3);
  
  
  // Set device 4 traffic control queue
  TrafficControlHelper tch;
  tch.SetRootQueueDisc ("ns3::FqCoDelQueueDisc", "MaxSize", QueueSizeValue (QueueSize (TCQUEUE)));
  tch.Install (d4d5);
  Config::ConnectWithoutContext ("/NodeList/4/$ns3::TrafficControlLayer/RootQueueDiscList/3/Drop", MakeCallback (&test));
  Config::ConnectWithoutContext ("/NodeList/4/$ns3::TrafficControlLayer/RootQueueDiscList/3/DropAfterEnqueue", MakeCallback (&test));
  Config::ConnectWithoutContext ("/NodeList/4/$ns3::TrafficControlLayer/RootQueueDiscList/3/DropBeforeEnqueue", MakeCallback (&testcb));
  
  //Set device 4 queue size
  PointerValue queue;
  d4d5.Get (0)->GetAttribute ("TxQueue", queue);
  Ptr<Queue<Packet>> txQueueD4 = queue.Get<Queue<Packet>> ();
  txQueueD4->SetAttribute("MaxSize", StringValue (TXQUEUE));
  
  //Link monitor
  //Tx
  d1d2.Get (0)->TraceConnectWithoutContext("PhyTxEnd", MakeBoundCallback(&linkPktCount, 1));
  d2d3.Get (0)->TraceConnectWithoutContext("PhyTxEnd", MakeBoundCallback(&linkPktCount, 2));
  d3d4.Get (0)->TraceConnectWithoutContext("PhyTxEnd", MakeBoundCallback(&linkPktCount, 3));
  d4d5.Get (0)->TraceConnectWithoutContext("PhyTxEnd", MakeBoundCallback(&linkPktCount, 4));
  //Rx
  d0d1.Get (1)->TraceConnectWithoutContext("PhyRxEnd", MakeBoundCallback(&linkPktCount, 5));
  da0c1.Get (1)->TraceConnectWithoutContext("PhyRxEnd", MakeBoundCallback(&linkPktCount, 5));
  d1d2.Get (1)->TraceConnectWithoutContext("PhyRxEnd", MakeBoundCallback(&linkPktCount, 6));
  da1c2.Get (1)->TraceConnectWithoutContext("PhyRxEnd", MakeBoundCallback(&linkPktCount, 6));
  d2d3.Get (1)->TraceConnectWithoutContext("PhyRxEnd", MakeBoundCallback(&linkPktCount, 7));
  da2c3.Get (1)->TraceConnectWithoutContext("PhyRxEnd", MakeBoundCallback(&linkPktCount, 7));
  d3d4.Get (1)->TraceConnectWithoutContext("PhyRxEnd", MakeBoundCallback(&linkPktCount, 8));


  Ipv4AddressHelper ipv4;
  ipv4.SetBase ("10.1.1.0", "255.255.255.0"); Ipv4InterfaceContainer i0i1 = ipv4.Assign (d0d1);
  ipv4.SetBase ("10.1.2.0", "255.255.255.0"); Ipv4InterfaceContainer i0i8 = ipv4.Assign (d0d8);
  ipv4.SetBase ("10.1.3.0", "255.255.255.0"); Ipv4InterfaceContainer i1i2 = ipv4.Assign (d1d2);
  ipv4.SetBase ("10.1.4.0", "255.255.255.0"); Ipv4InterfaceContainer i1i8 = ipv4.Assign (d1d8);
  ipv4.SetBase ("10.1.5.0", "255.255.255.0"); Ipv4InterfaceContainer i2i3 = ipv4.Assign (d2d3);
  ipv4.SetBase ("10.1.6.0", "255.255.255.0"); Ipv4InterfaceContainer i2i7 = ipv4.Assign (d2d7);
  ipv4.SetBase ("10.1.7.0", "255.255.255.0"); Ipv4InterfaceContainer i3i4 = ipv4.Assign (d3d4);
  ipv4.SetBase ("10.1.8.0", "255.255.255.0"); Ipv4InterfaceContainer i3i6 = ipv4.Assign (d3d6);
  ipv4.SetBase ("10.1.9.0", "255.255.255.0"); Ipv4InterfaceContainer i4i5 = ipv4.Assign (d4d5);
  ipv4.SetBase ("10.1.10.0", "255.255.255.0"); Ipv4InterfaceContainer i4i6 = ipv4.Assign (d4d6);
  ipv4.SetBase ("10.1.11.0", "255.255.255.0"); Ipv4InterfaceContainer i5i6 = ipv4.Assign (d5d6);
  ipv4.SetBase ("10.1.12.0", "255.255.255.0"); Ipv4InterfaceContainer i6i7 = ipv4.Assign (d6d7);
  ipv4.SetBase ("10.1.13.0", "255.255.255.0"); Ipv4InterfaceContainer i7i8 = ipv4.Assign (d7d8);

  ipv4.SetBase ("10.2.1.0", "255.255.255.0"); Ipv4InterfaceContainer ia0c1 = ipv4.Assign (da0c1);
  ipv4.SetBase ("10.2.2.0", "255.255.255.0"); Ipv4InterfaceContainer ia1c2 = ipv4.Assign (da1c2);
  ipv4.SetBase ("10.2.3.0", "255.255.255.0"); Ipv4InterfaceContainer ia2c3 = ipv4.Assign (da2c3);


  Ipv4GlobalRoutingHelper::PopulateRoutingTables ();


  // Set sink application
  uint16_t sinkPort = 9;
  Address sinkAddress (InetSocketAddress (i4i5.GetAddress (1), sinkPort));
  PacketSinkHelper packetSinkHelper ("ns3::TcpSocketFactory", InetSocketAddress (Ipv4Address::GetAny (), sinkPort));
  ApplicationContainer sinkApps = packetSinkHelper.Install (c.Get (5));
  sinkApps.Start (Seconds (0.));
  sinkApps.Stop (Seconds (END_TIME));


  // Set source application
  TypeId tid = TypeId::LookupByName ("ns3::TcpSocketFactory");
  for (int i = 0; i < 3; i++)
  {
    Ptr<MyApp> app = CreateObject<MyApp> ();
    Ptr<Node> node = ca.Get (i);
    app->Setup (tid, node ,sinkAddress, PACKET_SIZE, NUM_PACKETS, DataRate (DEFAULT_SEND_RATE),"Sender "+std::to_string(i));
    node->AddApplication (app);
    app->SetStartTime (Seconds (0));
    app->SetStopTime (Seconds (END_TIME - 1));
  }

  // Trace settings
  AsciiTraceHelper ascii;

  p2p_nr.EnableAsciiAll (ascii.CreateFileStream ("./Data/simple.tr"));
  streamLinkUtil = ascii.CreateFileStream ("./Data/link45-simple.util");
  streamLinkLoss = ascii.CreateFileStream ("./Data/link45-simple.loss");
  Simulator::Schedule(Time (Seconds (30)), &monitorLink, 30, 5000000);


  // Flow Monitor
  FlowMonitorHelper flowmonHelper;
  if (enableFlowMonitor)
  {
    flowmonHelper.InstallAll ();
  }
  config.ConfigureAttributes ();
  Simulator::Stop (Seconds (END_TIME));
  Simulator::Run ();


  if (enableFlowMonitor)
  {
    flowmonHelper.SerializeToXmlFile ("./Data/simple.flowmon", false, false);
  }
  

  Simulator::Destroy ();
  return 0;
}