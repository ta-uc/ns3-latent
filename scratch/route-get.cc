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


#include "Eigen/Core"
#include "Eigen/LU"

#define PACKET_SIZE 1300 //bytes 分割・統合されないサイズにする
#define SEGMENT_SIZE 1300 //bytes この大きさのデータがたまると送信される
#define ONE_DATUM 1 //パケットで1データ
#define DEFAULT_SEND_RATE "5Mbps"
#define BOTTLE_NECK_LINK_RATE "5Mbps"
#define OTHER_LINK_RATE "10Mbps"
#define NUM_PACKETS 1
#define END_TIME 30 //Seconds
#define TXQUEUE "5p" //先にうまる
#define TCQUEUE "5p" //TXが埋まると使われる
#define TCP_TYPE "ns3::TcpNewReno"


using namespace ns3;

using namespace Eigen;


NS_LOG_COMPONENT_DEFINE ("Internet2");


uint16_t row = 0;

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
    m_targetRate (0)
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
    if (row <= 109)
    {
      row++;
    }
    StopApplication ();
    ChangeDataRate (m_packetLoss / (double) m_tcpsent);
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
  uint64_t dataRateNow = m_dataRate.GetBitRate ();
  if (m_previousLossRate < 0.001 && dataRateNow < m_targetRate)
  {
    m_dataRate = DataRate(m_targetRate * (1 / exp(-11 * lossRate)));
  }else{
    m_dataRate =  DataRate(static_cast<uint64_t>(dataRateNow * exp (-11 * lossRate)));
  }
}

void
MyApp::DetectPacketLoss (const uint32_t org, const uint32_t cgd)
{
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

// variables for packet count
  // uint64_t n0n1tx_t = 0, n0n3tx_t = 0, n1n0tx_t = 0, n1n3tx_t = 0, n1n2tx_t = 0,
  // uint64_t n2n1tx_t = 0, n2n5tx_t = 0, n3n0tx_t = 0, n3n1tx_t = 0, n3n4tx_t = 0,
  // uint64_t n4n3tx_t = 0, n4n5tx_t = 0, n4n6tx_t = 0, n5n2tx_t = 0, n5n4tx_t = 0, n5n8tx_t = 0,
  // uint64_t n6n4tx_t = 0, n6n7tx_t = 0, n6n8tx_t = 0, n7n6tx_t = 0, n7n10tx_t = 0,
  // uint64_t n8n5tx_t = 0, n8n6tx_t = 0, n8n9tx_t = 0, n9n8tx_t = 0, n9n10tx_t = 0
  // uint64_t n10n7tx_t = 0, n10n9tx_t = 0
std::array<uint64_t, 28> pktCountAry = {0};

int rt[112][28] = {{0}};

static void
linkPktCount (uint16_t linkn, Ptr< const Packet > packet)
{
  if (packet->GetSize () > 1000)
  {
    rt[row][linkn-1] = {1};
  }
}


int 
main (int argc, char *argv[])
{
  CommandLine cmd;
  bool enableFlowMonitor = false;
  bool enableAnimation = false;
  cmd.AddValue ("EnableMonitor", "Enable Flow Monitor", enableFlowMonitor);
  cmd.AddValue ("EnableAnimation", "Enable Animation", enableAnimation);
  cmd.Parse (argc, argv);

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
  p2p.SetDeviceAttribute ("DataRate", StringValue (OTHER_LINK_RATE));
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


  // Link monitor reg
    //link0->1
    d0d1.Get (0)->TraceConnectWithoutContext("PhyTxEnd", MakeBoundCallback(&linkPktCount, 1));
    //link0->3
    d0d3.Get (0)->TraceConnectWithoutContext("PhyTxEnd", MakeBoundCallback(&linkPktCount, 2));
    //link0<-1
    d0d1.Get (1)->TraceConnectWithoutContext("PhyTxEnd", MakeBoundCallback(&linkPktCount, 3));
    //link1->2
    d1d2.Get (0)->TraceConnectWithoutContext("PhyTxEnd", MakeBoundCallback(&linkPktCount, 4));
    //link1->3
    d1d3.Get (0)->TraceConnectWithoutContext("PhyTxEnd", MakeBoundCallback(&linkPktCount, 5));  
    //link1<-2
    d1d2.Get (1)->TraceConnectWithoutContext("PhyTxEnd", MakeBoundCallback(&linkPktCount, 6));
    //link2->5
    d2d5.Get (0)->TraceConnectWithoutContext("PhyTxEnd", MakeBoundCallback(&linkPktCount, 7));
    //link0<-3
    d0d3.Get (1)->TraceConnectWithoutContext("PhyTxEnd", MakeBoundCallback(&linkPktCount, 8));
    //link1<-3
    d1d3.Get (1)->TraceConnectWithoutContext("PhyTxEnd", MakeBoundCallback(&linkPktCount, 9));  
    //link3->4
    d3d4.Get (0)->TraceConnectWithoutContext("PhyTxEnd", MakeBoundCallback(&linkPktCount, 10));
    //link3<-4
    d3d4.Get (1)->TraceConnectWithoutContext("PhyTxEnd", MakeBoundCallback(&linkPktCount, 11));
    //link4->6
    d4d6.Get (0)->TraceConnectWithoutContext("PhyTxEnd", MakeBoundCallback(&linkPktCount, 12));
    //link4->5
    d4d5.Get (0)->TraceConnectWithoutContext("PhyTxEnd", MakeBoundCallback(&linkPktCount, 13));
    //link2<-5
    d2d5.Get (1)->TraceConnectWithoutContext("PhyTxEnd", MakeBoundCallback(&linkPktCount, 14));
    //link5->8
    d5d8.Get (0)->TraceConnectWithoutContext("PhyTxEnd", MakeBoundCallback(&linkPktCount, 15));
    //link4<-5
    d4d5.Get (1)->TraceConnectWithoutContext("PhyTxEnd", MakeBoundCallback(&linkPktCount, 16));
    //link4<-6
    d4d6.Get (1)->TraceConnectWithoutContext("PhyTxEnd", MakeBoundCallback(&linkPktCount, 17));
    //link6->8
    d6d8.Get (0)->TraceConnectWithoutContext("PhyTxEnd", MakeBoundCallback(&linkPktCount, 18));
    //link6->7
    d6d7.Get (0)->TraceConnectWithoutContext("PhyTxEnd", MakeBoundCallback(&linkPktCount, 19));
    //link6<-7
    d6d7.Get (1)->TraceConnectWithoutContext("PhyTxEnd", MakeBoundCallback(&linkPktCount, 20));
    //link7->10
    d7d10.Get (0)->TraceConnectWithoutContext("PhyTxEnd", MakeBoundCallback(&linkPktCount, 21));
    //link5<-8
    d5d8.Get (1)->TraceConnectWithoutContext("PhyTxEnd", MakeBoundCallback(&linkPktCount, 22));
    //link6<-8
    d6d8.Get (1)->TraceConnectWithoutContext("PhyTxEnd", MakeBoundCallback(&linkPktCount, 23));
    //link8->9
    d8d9.Get (0)->TraceConnectWithoutContext("PhyTxEnd", MakeBoundCallback(&linkPktCount, 24));
    //link8<-9
    d8d9.Get (1)->TraceConnectWithoutContext("PhyTxEnd", MakeBoundCallback(&linkPktCount, 25));
    //link9->10
    d9d10.Get (0)->TraceConnectWithoutContext("PhyTxEnd", MakeBoundCallback(&linkPktCount, 26));
    //link7<-10
    d7d10.Get (1)->TraceConnectWithoutContext("PhyTxEnd", MakeBoundCallback(&linkPktCount, 27));
    //link9<-10
    d9d10.Get (1)->TraceConnectWithoutContext("PhyTxEnd", MakeBoundCallback(&linkPktCount, 28));
  // Link monitor reg end


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
            app->Setup (tid, node ,sinkAddress, PACKET_SIZE, NUM_PACKETS, DataRate (DEFAULT_SEND_RATE),"Sender "+std::to_string(i)+" (" + std::to_string(i) + "-" + std::to_string(j) + ")");
            node->AddApplication (app);
            app->SetStartTime (Seconds (i*2+j*0.1));
            app->SetStopTime (Seconds (END_TIME -1));
          }
        }
      }
  // Setup source applications end

  // Animation settings
    AnimationInterface::SetConstantPosition (c.Get (0),2.0,2.0);
    AnimationInterface::SetConstantPosition (c.Get (1),2.0,4.0);
    AnimationInterface::SetConstantPosition (c.Get (2),3.0,6.0);
    AnimationInterface::SetConstantPosition (c.Get (3),4.0,4.0);
    AnimationInterface::SetConstantPosition (c.Get (4),6.0,4.0);
    AnimationInterface::SetConstantPosition (c.Get (5),6.0,6.0);
    AnimationInterface::SetConstantPosition (c.Get (6),8.0,4.0);
    AnimationInterface::SetConstantPosition (c.Get (7),8.0,3.0);
    AnimationInterface::SetConstantPosition (c.Get (8),8.0,6.0);
    AnimationInterface::SetConstantPosition (c.Get (9),9.0,5.0);
    AnimationInterface::SetConstantPosition (c.Get (10),10.0,4.0);
    AnimationInterface anim ("./Data/routeget.xml");
  //Animation settings end

  Simulator::Stop (Seconds (END_TIME));
  Simulator::Run ();
std::cout << "[";
  for (int i = 1; i < 111; i++)
  {
    std::cout << "[";
    for (int j = 0; j < 28; j++)
    {
      std::cout << rt[i][j];
      if (j != 27)
      {
        std::cout << ",";
      }
      
    }
    std::cout << "],"<<std::endl;
  }
  std::cout << "]";
  Simulator::Destroy ();
  return 0;
}