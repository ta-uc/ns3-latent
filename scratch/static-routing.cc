#include <iostream>
#include <fstream>
#include <string>
#include <cassert>
#include <stdlib.h>
#include <time.h>
#include <tuple>
#include <vector>

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/csma-module.h"
#include "ns3/applications-module.h"
#include "ns3/ipv4-static-routing-helper.h"
#include "ns3/netanim-module.h"


using namespace ns3;

NS_LOG_COMPONENT_DEFINE ("Multipath staticrouging");
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

  if(++m_packetsSent % 10 == 0)   // １データ送信でコネクション終了
  {
    StopApplication ();
    // double lossRate = m_packetLoss / (double) m_tcpsent;
    // ChangeDataRate (lossRate);
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

typedef std::tuple<
  std::vector <int>,
  std::vector <double>
> rvector;

typedef std::tuple<
  Ptr<Ipv4StaticRouting>,
  Ipv4Address,
  Ipv4Address,
  int
> rArgs;

int 
main (int argc, char *argv[])
{
  CommandLine cmd;
  cmd.Parse (argc, argv);

  srand((unsigned)time(NULL));

  // std::vector<int> t;
  // std::vector<double> t2;

  // t = {1,2,3};
  // t2 = {0.8,0.1,0.1};
  // routing.push_back(std::make_tuple(t,t2));

  // t = {1,2,3};
  // t2 = {0,0,1};
  // routing.push_back(std::make_tuple(t,t2));

  NodeContainer c;
  c.Create (7);

  InternetStackHelper internet;
  internet.Install (c);

  // Point-to-point links
  NodeContainer nAnB = NodeContainer (c.Get (0), c.Get (1));
  NodeContainer nAnC = NodeContainer (c.Get (0), c.Get (2));
  NodeContainer nAnD = NodeContainer (c.Get (0), c.Get (3));
  NodeContainer nBnE = NodeContainer (c.Get (1), c.Get (4));
  NodeContainer nBnF = NodeContainer (c.Get (1), c.Get (5));
  NodeContainer nCnE = NodeContainer (c.Get (2), c.Get (4));
  NodeContainer nCnF = NodeContainer (c.Get (2), c.Get (5));
  NodeContainer nDnE = NodeContainer (c.Get (3), c.Get (4));
  NodeContainer nDnF = NodeContainer (c.Get (3), c.Get (5));
  NodeContainer nGnA = NodeContainer (c.Get (6), c.Get (0));

  PointToPointHelper p2p;
  p2p.SetDeviceAttribute ("DataRate", StringValue ("5Mbps"));
  p2p.SetChannelAttribute ("Delay", StringValue ("1ms"));
  NetDeviceContainer dAdB = p2p.Install (nAnB);
  NetDeviceContainer dAdC = p2p.Install (nAnC);
  NetDeviceContainer dAdD = p2p.Install (nAnD);
  NetDeviceContainer dBdE = p2p.Install (nBnE);
  NetDeviceContainer dBdF = p2p.Install (nBnF);
  NetDeviceContainer dCdE = p2p.Install (nCnE);
  NetDeviceContainer dCdF = p2p.Install (nCnF);
  NetDeviceContainer dDdE = p2p.Install (nDnE);
  NetDeviceContainer dDdF = p2p.Install (nDnF);
  NetDeviceContainer dGdA = p2p.Install (nGnA);

  //add IP addresses
  Ipv4AddressHelper ipv4;
  ipv4.SetBase ("10.1.1.0", "255.255.255.0");
  Ipv4InterfaceContainer iAiB = ipv4.Assign (dAdB);
  ipv4.SetBase ("10.1.2.0", "255.255.255.0");
  Ipv4InterfaceContainer iAiC = ipv4.Assign (dAdC);
  ipv4.SetBase ("10.1.3.0", "255.255.255.0");
  Ipv4InterfaceContainer iAiD = ipv4.Assign (dAdD);
  ipv4.SetBase ("10.1.4.0", "255.255.255.0");
  Ipv4InterfaceContainer iBiE = ipv4.Assign (dBdE);
  ipv4.SetBase ("10.1.5.0", "255.255.255.0");
  Ipv4InterfaceContainer iBiF = ipv4.Assign (dBdF);
  ipv4.SetBase ("10.1.6.0", "255.255.255.0");
  Ipv4InterfaceContainer iCiE = ipv4.Assign (dCdE);
  ipv4.SetBase ("10.1.7.0", "255.255.255.0");
  Ipv4InterfaceContainer iCiF = ipv4.Assign (dCdF);  
  ipv4.SetBase ("10.1.8.0", "255.255.255.0");
  Ipv4InterfaceContainer iDiE = ipv4.Assign (dDdE);  
  ipv4.SetBase ("10.1.9.0", "255.255.255.0");
  Ipv4InterfaceContainer iDiF = ipv4.Assign (dDdF);  
  ipv4.SetBase ("10.1.10.0", "255.255.255.0");
  Ipv4InterfaceContainer iGiA = ipv4.Assign (dGdA);
  //

  Ptr<Ipv4> ipv4A = c.Get (0)->GetObject<Ipv4> ();
  Ptr<Ipv4> ipv4B = c.Get (1)->GetObject<Ipv4> ();
  Ptr<Ipv4> ipv4C = c.Get (2)->GetObject<Ipv4> ();
  Ptr<Ipv4> ipv4D = c.Get (3)->GetObject<Ipv4> ();
  Ptr<Ipv4> ipv4G = c.Get (6)->GetObject<Ipv4> ();

  Ipv4StaticRoutingHelper ipv4RoutingHelper;
  Ptr<Ipv4StaticRouting> staticRoutingA = ipv4RoutingHelper.GetStaticRouting (ipv4A);
  Ptr<Ipv4StaticRouting> staticRoutingB = ipv4RoutingHelper.GetStaticRouting (ipv4B);
  Ptr<Ipv4StaticRouting> staticRoutingC = ipv4RoutingHelper.GetStaticRouting (ipv4C);
  Ptr<Ipv4StaticRouting> staticRoutingD = ipv4RoutingHelper.GetStaticRouting (ipv4D);
  Ptr<Ipv4StaticRouting> staticRoutingG = ipv4RoutingHelper.GetStaticRouting (ipv4G);

  Ipv4Address fromLocal = Ipv4Address ("102.102.102.102");
  // Ipv4Address AC_A = iAiC.GetAddress (0,0);
  // Ipv4Address AB_A = iAiB.GetAddress (0,0);
  // Ipv4Address AD_A = iAiD.GetAddress (0,0);
  // Ipv4Address BE_E = iBiE.GetAddress (1,0);
  Ipv4Address CF_F = iCiF.GetAddress (1,0);
  Ipv4Address GA_G = iGiA.GetAddress (0,0);

  rvector t,t2,t3,t4;
  t = rvector ({1},{1.0});
  t2 = rvector ({1,2,3},{0.33,0.33,0.33});
  t3 = rvector ({2},{1.0});

  // Create static routes from A to F (G->A->D->F) ///TEST
    staticRoutingG->AddHostRouteTo (CF_F, fromLocal, t);
    staticRoutingA->AddHostRouteTo (CF_F, GA_G, t2);
    staticRoutingD->AddHostRouteTo (CF_F, GA_G, t3);
  //

  // Create static routes from A to F (A->C->F)
    // staticRoutingA->AddHostRouteTo (CF_F, fromLocal, 2);
    // staticRoutingC->AddHostRouteTo (CF_F, AC_A, 3);
  //

  // Create static routes from A to E (A->B->E, A->D->E)
    // staticRoutingA->AddHostRouteTo (BE_E, fromLocal, 1); // A->B
    // staticRoutingB->AddHostRouteTo (BE_E, AB_A, 2);
    // staticRoutingD->AddHostRouteTo (BE_E, AD_A, 2);
  //

    // rArgs argA, argB;
    // argA = rArgs (staticRoutingA, BE_E, AB_A, 0);
    // argB = rArgs (staticRoutingA, CF_F, GA_G, 1);

    // Config::Connect ("/NodeList/0/$ns3::Ipv4L3Protocol/SendOutgoing", MakeBoundCallback(&ChangeRouteOwn, argA));
    // Config::Connect ("/NodeList/0/$ns3::Ipv4L3Protocol/UnicastForward", MakeBoundCallback(&ChangeRouteFromOther, argA));
    // Config::Connect ("/NodeList/0/$ns3::Ipv4L3Protocol/UnicastForward", MakeBoundCallback(&ChangeRouteFromOther, argB));
  
  //Install sink App
    uint16_t sinkPort = 9;
    PacketSinkHelper packetSinkHelper ("ns3::UdpSocketFactory", InetSocketAddress (Ipv4Address::GetAny (), sinkPort));
    ApplicationContainer sinkApp = packetSinkHelper.Install (c.Get (5));;
    sinkApp.Start (Seconds (0.));
    sinkApp.Stop (Seconds (10));
  //

  //Install Source App
    TypeId tid = TypeId::LookupByName ("ns3::UdpSocketFactory");
    // Ptr<MyApp> app = CreateObject<MyApp> ();
    // Ptr<Node> installTo = c.Get (0);
    // Address sinkAddress = InetSocketAddress (iCiF.GetAddress (1), sinkPort);
    // app->Setup (tid, installTo ,sinkAddress, 1300, 50, DataRate ("500Kbps"), "A->C->F");
    // installTo->AddApplication (app);
    // app->SetStartTime (Seconds (0));
    // app->SetStopTime (Seconds (10));

    Ptr<MyApp> app2 = CreateObject<MyApp> ();
    Ptr<Node> installTo2 = c.Get (6);
    Address sinkAddress2 = InetSocketAddress (iCiF.GetAddress (1), sinkPort);
    app2->Setup (tid, installTo2 ,sinkAddress2, 1300, 50, DataRate ("500Kbps"), "G->A->D->F");
    installTo2->AddApplication (app2);
    app2->SetStartTime (Seconds (0));
    app2->SetStopTime (Seconds (10));

    // Ptr<MyApp> app3 = CreateObject<MyApp> ();
    // Ptr<Node> installTo3 = c.Get (0);
    // Address sinkAddress3 = InetSocketAddress (iBiE.GetAddress (1), sinkPort);
    // app3->Setup (tid, installTo3 ,sinkAddress3, 1300, 50, DataRate ("500Kbps"), "A->E");
    // installTo3->AddApplication (app3);
    // app3->SetStartTime (Seconds (0));
    // app3->SetStopTime (Seconds (10));
  //

  // Animation settings
    AnimationInterface::SetConstantPosition (c.Get (0),1.0,2.0);
    AnimationInterface::SetConstantPosition (c.Get (1),3.0,1.0);
    AnimationInterface::SetConstantPosition (c.Get (2),3.0,2.0);
    AnimationInterface::SetConstantPosition (c.Get (3),3.0,3.0);
    AnimationInterface::SetConstantPosition (c.Get (4),5.0,1.5);
    AnimationInterface::SetConstantPosition (c.Get (5),5.0,2.5);
    AnimationInterface::SetConstantPosition (c.Get (6),0.0,2.0);
    AnimationInterface anim ("./Data/static-route.xml");
  //Animation settings end

 AsciiTraceHelper ascii;
//  p2p.EnableAsciiAll (ascii.CreateFileStream ("static-routing-slash32.tr"));
//  p2p.EnablePcapAll ("static-routing-slash32");
  Simulator::Stop (Seconds (10));
  Simulator::Run ();
  Simulator::Destroy ();
  return 0;
}