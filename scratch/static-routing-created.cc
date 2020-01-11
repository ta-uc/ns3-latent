#include <iostream>
#include <fstream>
#include <string>
#include <cassert>
#include <stdlib.h>
#include <time.h>
#include <tuple>
#include <vector>
#include <array>
#include <map>

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

int 
main (int argc, char *argv[])
{
  CommandLine cmd;
  cmd.Parse (argc, argv);

  srand((unsigned)time(NULL));

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
  p2p.SetDeviceAttribute ("DataRate", StringValue ("5Mbps"));
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
  staticRoutingA->AddHostRouteTo (iAB.GetAddress (1,0), fromLocal, rvector ({1},{1}));//A->B
  staticRoutingA->AddHostRouteTo (iAB.GetAddress (1,0), iAB.GetAddress (0,0), rvector ({1},{1}));//A->B

  staticRoutingA->AddHostRouteTo (iAC.GetAddress (1,0), fromLocal, rvector ({2},{1}));//A->C
  staticRoutingA->AddHostRouteTo (iAC.GetAddress (1,0), iAC.GetAddress (0,0), rvector ({2},{1}));//A->C

  staticRoutingA->AddHostRouteTo (iBD.GetAddress (1,0), fromLocal, rvector ({1},{1}));//A->B
  staticRoutingA->AddHostRouteTo (iBD.GetAddress (1,0), iAB.GetAddress (0,0), rvector ({1},{1}));//A->B
  staticRoutingB->AddHostRouteTo (iBD.GetAddress (1,0), iAB.GetAddress (0,0), rvector ({3},{1}));//B->D

  staticRoutingA->AddHostRouteTo (iCE.GetAddress (1,0), fromLocal, rvector ({2},{1}));//A->C
  staticRoutingA->AddHostRouteTo (iCE.GetAddress (1,0), iAC.GetAddress (0,0), rvector ({2},{1}));//A->C
  staticRoutingC->AddHostRouteTo (iCE.GetAddress (1,0), iAC.GetAddress (0,0), rvector ({3},{1}));//C->E

  staticRoutingA->AddHostRouteTo (iDF.GetAddress (1,0), fromLocal, rvector ({1},{1}));//A->B
  staticRoutingA->AddHostRouteTo (iDF.GetAddress (1,0), iAB.GetAddress (0,0), rvector ({1},{1}));//A->B
  staticRoutingB->AddHostRouteTo (iDF.GetAddress (1,0), iAB.GetAddress (0,0), rvector ({3},{1}));//B->D
  staticRoutingD->AddHostRouteTo (iDF.GetAddress (1,0), iAB.GetAddress (0,0), rvector ({2},{1}));//D->F

  staticRoutingA->AddHostRouteTo (iGH.GetAddress (0,0), fromLocal, rvector ({2},{1}));//A->C
  staticRoutingA->AddHostRouteTo (iGH.GetAddress (0,0), iAC.GetAddress (0,0), rvector ({2},{1}));//A->C
  staticRoutingC->AddHostRouteTo (iGH.GetAddress (0,0), iAC.GetAddress (0,0), rvector ({3},{1}));//C->E
  staticRoutingE->AddHostRouteTo (iGH.GetAddress (0,0), iAC.GetAddress (0,0), rvector ({3},{1}));//E->H
  staticRoutingH->AddHostRouteTo (iGH.GetAddress (0,0), iAC.GetAddress (0,0), rvector ({2},{1}));//H->G

  staticRoutingA->AddHostRouteTo (iEH.GetAddress (1,0), fromLocal, rvector ({2},{1}));//A->C
  staticRoutingA->AddHostRouteTo (iEH.GetAddress (1,0), iAC.GetAddress (0,0), rvector ({2},{1}));//A->C
  staticRoutingC->AddHostRouteTo (iEH.GetAddress (1,0), iAC.GetAddress (0,0), rvector ({3},{1}));//C->E
  staticRoutingE->AddHostRouteTo (iEH.GetAddress (1,0), iAC.GetAddress (0,0), rvector ({3},{1}));//E->H

  staticRoutingA->AddHostRouteTo (iFI.GetAddress (1,0), fromLocal, rvector ({1},{1}));//A->B
  staticRoutingA->AddHostRouteTo (iFI.GetAddress (1,0), iAB.GetAddress (0,0), rvector ({1},{1}));//A->B
  staticRoutingB->AddHostRouteTo (iFI.GetAddress (1,0), iAB.GetAddress (0,0), rvector ({3},{1}));//B->D
  staticRoutingD->AddHostRouteTo (iFI.GetAddress (1,0), iAB.GetAddress (0,0), rvector ({2},{1}));//D->F
  staticRoutingF->AddHostRouteTo (iFI.GetAddress (1,0), iAB.GetAddress (0,0), rvector ({3},{1}));//F->I

  staticRoutingA->AddHostRouteTo (iIJ.GetAddress (1,0), fromLocal, rvector ({1},{1}));//A->B
  staticRoutingA->AddHostRouteTo (iIJ.GetAddress (1,0), iAB.GetAddress (0,0), rvector ({1},{1}));//A->B
  staticRoutingB->AddHostRouteTo (iIJ.GetAddress (1,0), iAB.GetAddress (0,0), rvector ({3},{1}));//B->D
  staticRoutingD->AddHostRouteTo (iIJ.GetAddress (1,0), iAB.GetAddress (0,0), rvector ({2},{1}));//D->F
  staticRoutingF->AddHostRouteTo (iIJ.GetAddress (1,0), iAB.GetAddress (0,0), rvector ({3},{1}));//F->I
  staticRoutingI->AddHostRouteTo (iIJ.GetAddress (1,0), iAB.GetAddress (0,0), rvector ({3},{1}));//I->J

  staticRoutingA->AddHostRouteTo (iGK.GetAddress (1,0), fromLocal, rvector ({2},{1}));//A->C
  staticRoutingA->AddHostRouteTo (iGK.GetAddress (1,0), iAC.GetAddress (0,0), rvector ({2},{1}));//A->C
  staticRoutingC->AddHostRouteTo (iGK.GetAddress (1,0), iAC.GetAddress (0,0), rvector ({3},{1}));//C->E
  staticRoutingE->AddHostRouteTo (iGK.GetAddress (1,0), iAC.GetAddress (0,0), rvector ({3},{1}));//E->H
  staticRoutingH->AddHostRouteTo (iGK.GetAddress (1,0), iAC.GetAddress (0,0), rvector ({2},{1}));//H->G
  staticRoutingG->AddHostRouteTo (iGK.GetAddress (1,0), iAC.GetAddress (0,0), rvector ({2},{1}));//G->K

  staticRoutingB->AddHostRouteTo (iAB.GetAddress (0,0), fromLocal, rvector ({1},{1}));//B->A
  staticRoutingB->AddHostRouteTo (iAB.GetAddress (0,0), iAB.GetAddress (1,0), rvector ({1},{1}));//B->A

  staticRoutingB->AddHostRouteTo (iBC.GetAddress (1,0), fromLocal, rvector ({2},{1}));//B->C
  staticRoutingB->AddHostRouteTo (iBC.GetAddress (1,0), iBC.GetAddress (0,0), rvector ({2},{1}));//B->C

  staticRoutingB->AddHostRouteTo (iBD.GetAddress (1,0), fromLocal, rvector ({3},{1}));//B->D
  staticRoutingB->AddHostRouteTo (iBD.GetAddress (1,0), iBD.GetAddress (0,0), rvector ({3},{1}));//B->D

  staticRoutingB->AddHostRouteTo (iCE.GetAddress (1,0), fromLocal, rvector ({2},{1}));//B->C
  staticRoutingB->AddHostRouteTo (iCE.GetAddress (1,0), iBC.GetAddress (0,0), rvector ({2},{1}));//B->C
  staticRoutingC->AddHostRouteTo (iCE.GetAddress (1,0), iBC.GetAddress (0,0), rvector ({3},{1}));//C->E

  staticRoutingB->AddHostRouteTo (iDF.GetAddress (1,0), fromLocal, rvector ({3},{1}));//B->D
  staticRoutingB->AddHostRouteTo (iDF.GetAddress (1,0), iBD.GetAddress (0,0), rvector ({3},{1}));//B->D
  staticRoutingD->AddHostRouteTo (iDF.GetAddress (1,0), iBD.GetAddress (0,0), rvector ({2},{1}));//D->F

  staticRoutingB->AddHostRouteTo (iGH.GetAddress (0,0), fromLocal, rvector ({2},{1}));//B->C
  staticRoutingB->AddHostRouteTo (iGH.GetAddress (0,0), iBC.GetAddress (0,0), rvector ({2},{1}));//B->C
  staticRoutingC->AddHostRouteTo (iGH.GetAddress (0,0), iBC.GetAddress (0,0), rvector ({3},{1}));//C->E
  staticRoutingE->AddHostRouteTo (iGH.GetAddress (0,0), iBC.GetAddress (0,0), rvector ({3},{1}));//E->H
  staticRoutingH->AddHostRouteTo (iGH.GetAddress (0,0), iBC.GetAddress (0,0), rvector ({2},{1}));//H->G

  staticRoutingB->AddHostRouteTo (iEH.GetAddress (1,0), fromLocal, rvector ({2},{1}));//B->C
  staticRoutingB->AddHostRouteTo (iEH.GetAddress (1,0), iBC.GetAddress (0,0), rvector ({2},{1}));//B->C
  staticRoutingC->AddHostRouteTo (iEH.GetAddress (1,0), iBC.GetAddress (0,0), rvector ({3},{1}));//C->E
  staticRoutingE->AddHostRouteTo (iEH.GetAddress (1,0), iBC.GetAddress (0,0), rvector ({3},{1}));//E->H

  staticRoutingB->AddHostRouteTo (iHI.GetAddress (1,0), fromLocal, rvector ({2},{1}));//B->C
  staticRoutingB->AddHostRouteTo (iHI.GetAddress (1,0), iBC.GetAddress (0,0), rvector ({2},{1}));//B->C
  staticRoutingC->AddHostRouteTo (iHI.GetAddress (1,0), iBC.GetAddress (0,0), rvector ({3},{1}));//C->E
  staticRoutingE->AddHostRouteTo (iHI.GetAddress (1,0), iBC.GetAddress (0,0), rvector ({3},{1}));//E->H
  staticRoutingH->AddHostRouteTo (iHI.GetAddress (1,0), iBC.GetAddress (0,0), rvector ({3},{1}));//H->I

  staticRoutingB->AddHostRouteTo (iIJ.GetAddress (1,0), fromLocal, rvector ({3},{1}));//B->D
  staticRoutingB->AddHostRouteTo (iIJ.GetAddress (1,0), iBD.GetAddress (0,0), rvector ({3},{1}));//B->D
  staticRoutingD->AddHostRouteTo (iIJ.GetAddress (1,0), iBD.GetAddress (0,0), rvector ({2},{1}));//D->F
  staticRoutingF->AddHostRouteTo (iIJ.GetAddress (1,0), iBD.GetAddress (0,0), rvector ({3},{1}));//F->I
  staticRoutingI->AddHostRouteTo (iIJ.GetAddress (1,0), iBD.GetAddress (0,0), rvector ({3},{1}));//I->J

  staticRoutingB->AddHostRouteTo (iJK.GetAddress (1,0), fromLocal, rvector ({3},{1}));//B->D
  staticRoutingB->AddHostRouteTo (iJK.GetAddress (1,0), iBD.GetAddress (0,0), rvector ({3},{1}));//B->D
  staticRoutingD->AddHostRouteTo (iJK.GetAddress (1,0), iBD.GetAddress (0,0), rvector ({2},{1}));//D->F
  staticRoutingF->AddHostRouteTo (iJK.GetAddress (1,0), iBD.GetAddress (0,0), rvector ({3},{1}));//F->I
  staticRoutingI->AddHostRouteTo (iJK.GetAddress (1,0), iBD.GetAddress (0,0), rvector ({3},{1}));//I->J
  staticRoutingJ->AddHostRouteTo (iJK.GetAddress (1,0), iBD.GetAddress (0,0), rvector ({2},{1}));//J->K

  staticRoutingC->AddHostRouteTo (iAC.GetAddress (0,0), fromLocal, rvector ({1},{1}));//C->A
  staticRoutingC->AddHostRouteTo (iAC.GetAddress (0,0), iAC.GetAddress (1,0), rvector ({1},{1}));//C->A

  staticRoutingC->AddHostRouteTo (iBC.GetAddress (0,0), fromLocal, rvector ({2},{1}));//C->B
  staticRoutingC->AddHostRouteTo (iBC.GetAddress (0,0), iBC.GetAddress (1,0), rvector ({2},{1}));//C->B

  staticRoutingC->AddHostRouteTo (iBD.GetAddress (1,0), fromLocal, rvector ({2},{1}));//C->B
  staticRoutingC->AddHostRouteTo (iBD.GetAddress (1,0), iBC.GetAddress (1,0), rvector ({2},{1}));//C->B
  staticRoutingB->AddHostRouteTo (iBD.GetAddress (1,0), iBC.GetAddress (1,0), rvector ({3},{1}));//B->D

  staticRoutingC->AddHostRouteTo (iCE.GetAddress (1,0), fromLocal, rvector ({3},{1}));//C->E
  staticRoutingC->AddHostRouteTo (iCE.GetAddress (1,0), iCE.GetAddress (0,0), rvector ({3},{1}));//C->E

  staticRoutingC->AddHostRouteTo (iEF.GetAddress (1,0), fromLocal, rvector ({3},{1}));//C->E
  staticRoutingC->AddHostRouteTo (iEF.GetAddress (1,0), iCE.GetAddress (0,0), rvector ({3},{1}));//C->E
  staticRoutingE->AddHostRouteTo (iEF.GetAddress (1,0), iCE.GetAddress (0,0), rvector ({2},{1}));//E->F

  staticRoutingC->AddHostRouteTo (iGH.GetAddress (0,0), fromLocal, rvector ({3},{1}));//C->E
  staticRoutingC->AddHostRouteTo (iGH.GetAddress (0,0), iCE.GetAddress (0,0), rvector ({3},{1}));//C->E
  staticRoutingE->AddHostRouteTo (iGH.GetAddress (0,0), iCE.GetAddress (0,0), rvector ({3},{1}));//E->H
  staticRoutingH->AddHostRouteTo (iGH.GetAddress (0,0), iCE.GetAddress (0,0), rvector ({2},{1}));//H->G

  staticRoutingC->AddHostRouteTo (iEH.GetAddress (1,0), fromLocal, rvector ({3},{1}));//C->E
  staticRoutingC->AddHostRouteTo (iEH.GetAddress (1,0), iCE.GetAddress (0,0), rvector ({3},{1}));//C->E
  staticRoutingE->AddHostRouteTo (iEH.GetAddress (1,0), iCE.GetAddress (0,0), rvector ({3},{1}));//E->H

  staticRoutingC->AddHostRouteTo (iFI.GetAddress (1,0), fromLocal, rvector ({3},{1}));//C->E
  staticRoutingC->AddHostRouteTo (iFI.GetAddress (1,0), iCE.GetAddress (0,0), rvector ({3},{1}));//C->E
  staticRoutingE->AddHostRouteTo (iFI.GetAddress (1,0), iCE.GetAddress (0,0), rvector ({2},{1}));//E->F
  staticRoutingF->AddHostRouteTo (iFI.GetAddress (1,0), iCE.GetAddress (0,0), rvector ({3},{1}));//F->I

  staticRoutingC->AddHostRouteTo (iIJ.GetAddress (1,0), fromLocal, rvector ({3},{1}));//C->E
  staticRoutingC->AddHostRouteTo (iIJ.GetAddress (1,0), iCE.GetAddress (0,0), rvector ({3},{1}));//C->E
  staticRoutingE->AddHostRouteTo (iIJ.GetAddress (1,0), iCE.GetAddress (0,0), rvector ({2},{1}));//E->F
  staticRoutingF->AddHostRouteTo (iIJ.GetAddress (1,0), iCE.GetAddress (0,0), rvector ({3},{1}));//F->I
  staticRoutingI->AddHostRouteTo (iIJ.GetAddress (1,0), iCE.GetAddress (0,0), rvector ({3},{1}));//I->J

  staticRoutingC->AddHostRouteTo (iGK.GetAddress (1,0), fromLocal, rvector ({3},{1}));//C->E
  staticRoutingC->AddHostRouteTo (iGK.GetAddress (1,0), iCE.GetAddress (0,0), rvector ({3},{1}));//C->E
  staticRoutingE->AddHostRouteTo (iGK.GetAddress (1,0), iCE.GetAddress (0,0), rvector ({3},{1}));//E->H
  staticRoutingH->AddHostRouteTo (iGK.GetAddress (1,0), iCE.GetAddress (0,0), rvector ({2},{1}));//H->G
  staticRoutingG->AddHostRouteTo (iGK.GetAddress (1,0), iCE.GetAddress (0,0), rvector ({2},{1}));//G->K

  staticRoutingD->AddHostRouteTo (iAB.GetAddress (0,0), fromLocal, rvector ({1},{1}));//D->B
  staticRoutingD->AddHostRouteTo (iAB.GetAddress (0,0), iBD.GetAddress (1,0), rvector ({1},{1}));//D->B
  staticRoutingB->AddHostRouteTo (iAB.GetAddress (0,0), iBD.GetAddress (1,0), rvector ({1},{1}));//B->A

  staticRoutingD->AddHostRouteTo (iBD.GetAddress (0,0), fromLocal, rvector ({1},{1}));//D->B
  staticRoutingD->AddHostRouteTo (iBD.GetAddress (0,0), iBD.GetAddress (1,0), rvector ({1},{1}));//D->B

  staticRoutingD->AddHostRouteTo (iBC.GetAddress (1,0), fromLocal, rvector ({1},{1}));//D->B
  staticRoutingD->AddHostRouteTo (iBC.GetAddress (1,0), iBD.GetAddress (1,0), rvector ({1},{1}));//D->B
  staticRoutingB->AddHostRouteTo (iBC.GetAddress (1,0), iBD.GetAddress (1,0), rvector ({2},{1}));//B->C

  staticRoutingD->AddHostRouteTo (iEF.GetAddress (0,0), fromLocal, rvector ({2},{1}));//D->F
  staticRoutingD->AddHostRouteTo (iEF.GetAddress (0,0), iDF.GetAddress (0,0), rvector ({2},{1}));//D->F
  staticRoutingF->AddHostRouteTo (iEF.GetAddress (0,0), iDF.GetAddress (0,0), rvector ({1},{1}));//F->E

  staticRoutingD->AddHostRouteTo (iDF.GetAddress (1,0), fromLocal, rvector ({2},{1}));//D->F
  staticRoutingD->AddHostRouteTo (iDF.GetAddress (1,0), iDF.GetAddress (0,0), rvector ({2},{1}));//D->F

  staticRoutingD->AddHostRouteTo (iGH.GetAddress (0,0), fromLocal, rvector ({2},{1}));//D->F
  staticRoutingD->AddHostRouteTo (iGH.GetAddress (0,0), iDF.GetAddress (0,0), rvector ({2},{1}));//D->F
  staticRoutingF->AddHostRouteTo (iGH.GetAddress (0,0), iDF.GetAddress (0,0), rvector ({3},{1}));//F->I
  staticRoutingI->AddHostRouteTo (iGH.GetAddress (0,0), iDF.GetAddress (0,0), rvector ({2},{1}));//I->H
  staticRoutingH->AddHostRouteTo (iGH.GetAddress (0,0), iDF.GetAddress (0,0), rvector ({2},{1}));//H->G

  staticRoutingD->AddHostRouteTo (iEH.GetAddress (1,0), fromLocal, rvector ({2},{1}));//D->F
  staticRoutingD->AddHostRouteTo (iEH.GetAddress (1,0), iDF.GetAddress (0,0), rvector ({2},{1}));//D->F
  staticRoutingF->AddHostRouteTo (iEH.GetAddress (1,0), iDF.GetAddress (0,0), rvector ({1},{1}));//F->E
  staticRoutingE->AddHostRouteTo (iEH.GetAddress (1,0), iDF.GetAddress (0,0), rvector ({3},{1}));//E->H

  staticRoutingD->AddHostRouteTo (iFI.GetAddress (1,0), fromLocal, rvector ({2},{1}));//D->F
  staticRoutingD->AddHostRouteTo (iFI.GetAddress (1,0), iDF.GetAddress (0,0), rvector ({2},{1}));//D->F
  staticRoutingF->AddHostRouteTo (iFI.GetAddress (1,0), iDF.GetAddress (0,0), rvector ({3},{1}));//F->I

  staticRoutingD->AddHostRouteTo (iIJ.GetAddress (1,0), fromLocal, rvector ({2},{1}));//D->F
  staticRoutingD->AddHostRouteTo (iIJ.GetAddress (1,0), iDF.GetAddress (0,0), rvector ({2},{1}));//D->F
  staticRoutingF->AddHostRouteTo (iIJ.GetAddress (1,0), iDF.GetAddress (0,0), rvector ({3},{1}));//F->I
  staticRoutingI->AddHostRouteTo (iIJ.GetAddress (1,0), iDF.GetAddress (0,0), rvector ({3},{1}));//I->J

  staticRoutingD->AddHostRouteTo (iJK.GetAddress (1,0), fromLocal, rvector ({2},{1}));//D->F
  staticRoutingD->AddHostRouteTo (iJK.GetAddress (1,0), iDF.GetAddress (0,0), rvector ({2},{1}));//D->F
  staticRoutingF->AddHostRouteTo (iJK.GetAddress (1,0), iDF.GetAddress (0,0), rvector ({3},{1}));//F->I
  staticRoutingI->AddHostRouteTo (iJK.GetAddress (1,0), iDF.GetAddress (0,0), rvector ({3},{1}));//I->J
  staticRoutingJ->AddHostRouteTo (iJK.GetAddress (1,0), iDF.GetAddress (0,0), rvector ({2},{1}));//J->K

  staticRoutingE->AddHostRouteTo (iAC.GetAddress (0,0), fromLocal, rvector ({1},{1}));//E->C
  staticRoutingE->AddHostRouteTo (iAC.GetAddress (0,0), iCE.GetAddress (1,0), rvector ({1},{1}));//E->C
  staticRoutingC->AddHostRouteTo (iAC.GetAddress (0,0), iCE.GetAddress (1,0), rvector ({1},{1}));//C->A

  staticRoutingE->AddHostRouteTo (iBC.GetAddress (0,0), fromLocal, rvector ({1},{1}));//E->C
  staticRoutingE->AddHostRouteTo (iBC.GetAddress (0,0), iCE.GetAddress (1,0), rvector ({1},{1}));//E->C
  staticRoutingC->AddHostRouteTo (iBC.GetAddress (0,0), iCE.GetAddress (1,0), rvector ({2},{1}));//C->B

  staticRoutingE->AddHostRouteTo (iCE.GetAddress (0,0), fromLocal, rvector ({1},{1}));//E->C
  staticRoutingE->AddHostRouteTo (iCE.GetAddress (0,0), iCE.GetAddress (1,0), rvector ({1},{1}));//E->C

  staticRoutingE->AddHostRouteTo (iDF.GetAddress (0,0), fromLocal, rvector ({2},{1}));//E->F
  staticRoutingE->AddHostRouteTo (iDF.GetAddress (0,0), iEF.GetAddress (0,0), rvector ({2},{1}));//E->F
  staticRoutingF->AddHostRouteTo (iDF.GetAddress (0,0), iEF.GetAddress (0,0), rvector ({2},{1}));//F->D

  staticRoutingE->AddHostRouteTo (iEF.GetAddress (1,0), fromLocal, rvector ({2},{1}));//E->F
  staticRoutingE->AddHostRouteTo (iEF.GetAddress (1,0), iEF.GetAddress (0,0), rvector ({2},{1}));//E->F

  staticRoutingE->AddHostRouteTo (iGH.GetAddress (0,0), fromLocal, rvector ({3},{1}));//E->H
  staticRoutingE->AddHostRouteTo (iGH.GetAddress (0,0), iEH.GetAddress (0,0), rvector ({3},{1}));//E->H
  staticRoutingH->AddHostRouteTo (iGH.GetAddress (0,0), iEH.GetAddress (0,0), rvector ({2},{1}));//H->G

  staticRoutingE->AddHostRouteTo (iEH.GetAddress (1,0), fromLocal, rvector ({3},{1}));//E->H
  staticRoutingE->AddHostRouteTo (iEH.GetAddress (1,0), iEH.GetAddress (0,0), rvector ({3},{1}));//E->H

  staticRoutingE->AddHostRouteTo (iFI.GetAddress (1,0), fromLocal, rvector ({2},{1}));//E->F
  staticRoutingE->AddHostRouteTo (iFI.GetAddress (1,0), iEF.GetAddress (0,0), rvector ({2},{1}));//E->F
  staticRoutingF->AddHostRouteTo (iFI.GetAddress (1,0), iEF.GetAddress (0,0), rvector ({3},{1}));//F->I

  staticRoutingE->AddHostRouteTo (iIJ.GetAddress (1,0), fromLocal, rvector ({2},{1}));//E->F
  staticRoutingE->AddHostRouteTo (iIJ.GetAddress (1,0), iEF.GetAddress (0,0), rvector ({2},{1}));//E->F
  staticRoutingF->AddHostRouteTo (iIJ.GetAddress (1,0), iEF.GetAddress (0,0), rvector ({3},{1}));//F->I
  staticRoutingI->AddHostRouteTo (iIJ.GetAddress (1,0), iEF.GetAddress (0,0), rvector ({3},{1}));//I->J

  staticRoutingE->AddHostRouteTo (iGK.GetAddress (1,0), fromLocal, rvector ({3},{1}));//E->H
  staticRoutingE->AddHostRouteTo (iGK.GetAddress (1,0), iEH.GetAddress (0,0), rvector ({3},{1}));//E->H
  staticRoutingH->AddHostRouteTo (iGK.GetAddress (1,0), iEH.GetAddress (0,0), rvector ({2},{1}));//H->G
  staticRoutingG->AddHostRouteTo (iGK.GetAddress (1,0), iEH.GetAddress (0,0), rvector ({2},{1}));//G->K

  staticRoutingF->AddHostRouteTo (iAB.GetAddress (0,0), fromLocal, rvector ({2},{1}));//F->D
  staticRoutingF->AddHostRouteTo (iAB.GetAddress (0,0), iDF.GetAddress (1,0), rvector ({2},{1}));//F->D
  staticRoutingD->AddHostRouteTo (iAB.GetAddress (0,0), iDF.GetAddress (1,0), rvector ({1},{1}));//D->B
  staticRoutingB->AddHostRouteTo (iAB.GetAddress (0,0), iDF.GetAddress (1,0), rvector ({1},{1}));//B->A

  staticRoutingF->AddHostRouteTo (iBD.GetAddress (0,0), fromLocal, rvector ({2},{1}));//F->D
  staticRoutingF->AddHostRouteTo (iBD.GetAddress (0,0), iDF.GetAddress (1,0), rvector ({2},{1}));//F->D
  staticRoutingD->AddHostRouteTo (iBD.GetAddress (0,0), iDF.GetAddress (1,0), rvector ({1},{1}));//D->B

  staticRoutingF->AddHostRouteTo (iCE.GetAddress (0,0), fromLocal, rvector ({1},{1}));//F->E
  staticRoutingF->AddHostRouteTo (iCE.GetAddress (0,0), iEF.GetAddress (1,0), rvector ({1},{1}));//F->E
  staticRoutingE->AddHostRouteTo (iCE.GetAddress (0,0), iEF.GetAddress (1,0), rvector ({1},{1}));//E->C

  staticRoutingF->AddHostRouteTo (iDF.GetAddress (0,0), fromLocal, rvector ({2},{1}));//F->D
  staticRoutingF->AddHostRouteTo (iDF.GetAddress (0,0), iDF.GetAddress (1,0), rvector ({2},{1}));//F->D

  staticRoutingF->AddHostRouteTo (iEF.GetAddress (0,0), fromLocal, rvector ({1},{1}));//F->E
  staticRoutingF->AddHostRouteTo (iEF.GetAddress (0,0), iEF.GetAddress (1,0), rvector ({1},{1}));//F->E

  staticRoutingF->AddHostRouteTo (iGH.GetAddress (0,0), fromLocal, rvector ({1},{1}));//F->E
  staticRoutingF->AddHostRouteTo (iGH.GetAddress (0,0), iEF.GetAddress (1,0), rvector ({1},{1}));//F->E
  staticRoutingE->AddHostRouteTo (iGH.GetAddress (0,0), iEF.GetAddress (1,0), rvector ({3},{1}));//E->H
  staticRoutingH->AddHostRouteTo (iGH.GetAddress (0,0), iEF.GetAddress (1,0), rvector ({2},{1}));//H->G

  staticRoutingF->AddHostRouteTo (iHI.GetAddress (0,0), fromLocal, rvector ({3},{1}));//F->I
  staticRoutingF->AddHostRouteTo (iHI.GetAddress (0,0), iFI.GetAddress (0,0), rvector ({3},{1}));//F->I
  staticRoutingI->AddHostRouteTo (iHI.GetAddress (0,0), iFI.GetAddress (0,0), rvector ({2},{1}));//I->H

  staticRoutingF->AddHostRouteTo (iFI.GetAddress (1,0), fromLocal, rvector ({3},{1}));//F->I
  staticRoutingF->AddHostRouteTo (iFI.GetAddress (1,0), iFI.GetAddress (0,0), rvector ({3},{1}));//F->I

  staticRoutingF->AddHostRouteTo (iIJ.GetAddress (1,0), fromLocal, rvector ({3},{1}));//F->I
  staticRoutingF->AddHostRouteTo (iIJ.GetAddress (1,0), iFI.GetAddress (0,0), rvector ({3},{1}));//F->I
  staticRoutingI->AddHostRouteTo (iIJ.GetAddress (1,0), iFI.GetAddress (0,0), rvector ({3},{1}));//I->J

  staticRoutingF->AddHostRouteTo (iJK.GetAddress (1,0), fromLocal, rvector ({3},{1}));//F->I
  staticRoutingF->AddHostRouteTo (iJK.GetAddress (1,0), iFI.GetAddress (0,0), rvector ({3},{1}));//F->I
  staticRoutingI->AddHostRouteTo (iJK.GetAddress (1,0), iFI.GetAddress (0,0), rvector ({3},{1}));//I->J
  staticRoutingJ->AddHostRouteTo (iJK.GetAddress (1,0), iFI.GetAddress (0,0), rvector ({2},{1}));//J->K

  staticRoutingG->AddHostRouteTo (iAC.GetAddress (0,0), fromLocal, rvector ({1},{1}));//G->H
  staticRoutingG->AddHostRouteTo (iAC.GetAddress (0,0), iGH.GetAddress (0,0), rvector ({1},{1}));//G->H
  staticRoutingH->AddHostRouteTo (iAC.GetAddress (0,0), iGH.GetAddress (0,0), rvector ({1},{1}));//H->E
  staticRoutingE->AddHostRouteTo (iAC.GetAddress (0,0), iGH.GetAddress (0,0), rvector ({1},{1}));//E->C
  staticRoutingC->AddHostRouteTo (iAC.GetAddress (0,0), iGH.GetAddress (0,0), rvector ({1},{1}));//C->A

  staticRoutingG->AddHostRouteTo (iBC.GetAddress (0,0), fromLocal, rvector ({1},{1}));//G->H
  staticRoutingG->AddHostRouteTo (iBC.GetAddress (0,0), iGH.GetAddress (0,0), rvector ({1},{1}));//G->H
  staticRoutingH->AddHostRouteTo (iBC.GetAddress (0,0), iGH.GetAddress (0,0), rvector ({1},{1}));//H->E
  staticRoutingE->AddHostRouteTo (iBC.GetAddress (0,0), iGH.GetAddress (0,0), rvector ({1},{1}));//E->C
  staticRoutingC->AddHostRouteTo (iBC.GetAddress (0,0), iGH.GetAddress (0,0), rvector ({2},{1}));//C->B

  staticRoutingG->AddHostRouteTo (iCE.GetAddress (0,0), fromLocal, rvector ({1},{1}));//G->H
  staticRoutingG->AddHostRouteTo (iCE.GetAddress (0,0), iGH.GetAddress (0,0), rvector ({1},{1}));//G->H
  staticRoutingH->AddHostRouteTo (iCE.GetAddress (0,0), iGH.GetAddress (0,0), rvector ({1},{1}));//H->E
  staticRoutingE->AddHostRouteTo (iCE.GetAddress (0,0), iGH.GetAddress (0,0), rvector ({1},{1}));//E->C

  staticRoutingG->AddHostRouteTo (iDF.GetAddress (0,0), fromLocal, rvector ({1},{1}));//G->H
  staticRoutingG->AddHostRouteTo (iDF.GetAddress (0,0), iGH.GetAddress (0,0), rvector ({1},{1}));//G->H
  staticRoutingH->AddHostRouteTo (iDF.GetAddress (0,0), iGH.GetAddress (0,0), rvector ({1},{1}));//H->E
  staticRoutingE->AddHostRouteTo (iDF.GetAddress (0,0), iGH.GetAddress (0,0), rvector ({2},{1}));//E->F
  staticRoutingF->AddHostRouteTo (iDF.GetAddress (0,0), iGH.GetAddress (0,0), rvector ({2},{1}));//F->D

  staticRoutingG->AddHostRouteTo (iEH.GetAddress (0,0), fromLocal, rvector ({1},{1}));//G->H
  staticRoutingG->AddHostRouteTo (iEH.GetAddress (0,0), iGH.GetAddress (0,0), rvector ({1},{1}));//G->H
  staticRoutingH->AddHostRouteTo (iEH.GetAddress (0,0), iGH.GetAddress (0,0), rvector ({1},{1}));//H->E

  staticRoutingG->AddHostRouteTo (iFI.GetAddress (0,0), fromLocal, rvector ({1},{1}));//G->H
  staticRoutingG->AddHostRouteTo (iFI.GetAddress (0,0), iGH.GetAddress (0,0), rvector ({1},{1}));//G->H
  staticRoutingH->AddHostRouteTo (iFI.GetAddress (0,0), iGH.GetAddress (0,0), rvector ({3},{1}));//H->I
  staticRoutingI->AddHostRouteTo (iFI.GetAddress (0,0), iGH.GetAddress (0,0), rvector ({1},{1}));//I->F

  staticRoutingG->AddHostRouteTo (iGH.GetAddress (1,0), fromLocal, rvector ({1},{1}));//G->H
  staticRoutingG->AddHostRouteTo (iGH.GetAddress (1,0), iGH.GetAddress (0,0), rvector ({1},{1}));//G->H

  staticRoutingG->AddHostRouteTo (iHI.GetAddress (1,0), fromLocal, rvector ({1},{1}));//G->H
  staticRoutingG->AddHostRouteTo (iHI.GetAddress (1,0), iGH.GetAddress (0,0), rvector ({1},{1}));//G->H
  staticRoutingH->AddHostRouteTo (iHI.GetAddress (1,0), iGH.GetAddress (0,0), rvector ({3},{1}));//H->I

  staticRoutingG->AddHostRouteTo (iJK.GetAddress (0,0), fromLocal, rvector ({2},{1}));//G->K
  staticRoutingG->AddHostRouteTo (iJK.GetAddress (0,0), iGK.GetAddress (0,0), rvector ({2},{1}));//G->K
  staticRoutingK->AddHostRouteTo (iJK.GetAddress (0,0), iGK.GetAddress (0,0), rvector ({2},{1}));//K->J

  staticRoutingG->AddHostRouteTo (iGK.GetAddress (1,0), fromLocal, rvector ({2},{1}));//G->K
  staticRoutingG->AddHostRouteTo (iGK.GetAddress (1,0), iGK.GetAddress (0,0), rvector ({2},{1}));//G->K

  staticRoutingH->AddHostRouteTo (iAC.GetAddress (0,0), fromLocal, rvector ({1},{1}));//H->E
  staticRoutingH->AddHostRouteTo (iAC.GetAddress (0,0), iEH.GetAddress (1,0), rvector ({1},{1}));//H->E
  staticRoutingE->AddHostRouteTo (iAC.GetAddress (0,0), iEH.GetAddress (1,0), rvector ({1},{1}));//E->C
  staticRoutingC->AddHostRouteTo (iAC.GetAddress (0,0), iEH.GetAddress (1,0), rvector ({1},{1}));//C->A

  staticRoutingH->AddHostRouteTo (iBC.GetAddress (0,0), fromLocal, rvector ({1},{1}));//H->E
  staticRoutingH->AddHostRouteTo (iBC.GetAddress (0,0), iEH.GetAddress (1,0), rvector ({1},{1}));//H->E
  staticRoutingE->AddHostRouteTo (iBC.GetAddress (0,0), iEH.GetAddress (1,0), rvector ({1},{1}));//E->C
  staticRoutingC->AddHostRouteTo (iBC.GetAddress (0,0), iEH.GetAddress (1,0), rvector ({2},{1}));//C->B

  staticRoutingH->AddHostRouteTo (iCE.GetAddress (0,0), fromLocal, rvector ({1},{1}));//H->E
  staticRoutingH->AddHostRouteTo (iCE.GetAddress (0,0), iEH.GetAddress (1,0), rvector ({1},{1}));//H->E
  staticRoutingE->AddHostRouteTo (iCE.GetAddress (0,0), iEH.GetAddress (1,0), rvector ({1},{1}));//E->C

  staticRoutingH->AddHostRouteTo (iDF.GetAddress (0,0), fromLocal, rvector ({1},{1}));//H->E
  staticRoutingH->AddHostRouteTo (iDF.GetAddress (0,0), iEH.GetAddress (1,0), rvector ({1},{1}));//H->E
  staticRoutingE->AddHostRouteTo (iDF.GetAddress (0,0), iEH.GetAddress (1,0), rvector ({2},{1}));//E->F
  staticRoutingF->AddHostRouteTo (iDF.GetAddress (0,0), iEH.GetAddress (1,0), rvector ({2},{1}));//F->D

  staticRoutingH->AddHostRouteTo (iEH.GetAddress (0,0), fromLocal, rvector ({1},{1}));//H->E
  staticRoutingH->AddHostRouteTo (iEH.GetAddress (0,0), iEH.GetAddress (1,0), rvector ({1},{1}));//H->E

  staticRoutingH->AddHostRouteTo (iEF.GetAddress (1,0), fromLocal, rvector ({1},{1}));//H->E
  staticRoutingH->AddHostRouteTo (iEF.GetAddress (1,0), iEH.GetAddress (1,0), rvector ({1},{1}));//H->E
  staticRoutingE->AddHostRouteTo (iEF.GetAddress (1,0), iEH.GetAddress (1,0), rvector ({2},{1}));//E->F

  staticRoutingH->AddHostRouteTo (iGH.GetAddress (0,0), fromLocal, rvector ({2},{1}));//H->G
  staticRoutingH->AddHostRouteTo (iGH.GetAddress (0,0), iGH.GetAddress (1,0), rvector ({2},{1}));//H->G

  staticRoutingH->AddHostRouteTo (iHI.GetAddress (1,0), fromLocal, rvector ({3},{1}));//H->I
  staticRoutingH->AddHostRouteTo (iHI.GetAddress (1,0), iHI.GetAddress (0,0), rvector ({3},{1}));//H->I

  staticRoutingH->AddHostRouteTo (iIJ.GetAddress (1,0), fromLocal, rvector ({3},{1}));//H->I
  staticRoutingH->AddHostRouteTo (iIJ.GetAddress (1,0), iHI.GetAddress (0,0), rvector ({3},{1}));//H->I
  staticRoutingI->AddHostRouteTo (iIJ.GetAddress (1,0), iHI.GetAddress (0,0), rvector ({3},{1}));//I->J

  staticRoutingH->AddHostRouteTo (iGK.GetAddress (1,0), fromLocal, rvector ({2},{1}));//H->G
  staticRoutingH->AddHostRouteTo (iGK.GetAddress (1,0), iGH.GetAddress (1,0), rvector ({2},{1}));//H->G
  staticRoutingG->AddHostRouteTo (iGK.GetAddress (1,0), iGH.GetAddress (1,0), rvector ({2},{1}));//G->K

  staticRoutingI->AddHostRouteTo (iAC.GetAddress (0,0), fromLocal, rvector ({2},{1}));//I->H
  staticRoutingI->AddHostRouteTo (iAC.GetAddress (0,0), iHI.GetAddress (1,0), rvector ({2},{1}));//I->H
  staticRoutingH->AddHostRouteTo (iAC.GetAddress (0,0), iHI.GetAddress (1,0), rvector ({1},{1}));//H->E
  staticRoutingE->AddHostRouteTo (iAC.GetAddress (0,0), iHI.GetAddress (1,0), rvector ({1},{1}));//E->C
  staticRoutingC->AddHostRouteTo (iAC.GetAddress (0,0), iHI.GetAddress (1,0), rvector ({1},{1}));//C->A

  staticRoutingI->AddHostRouteTo (iBD.GetAddress (0,0), fromLocal, rvector ({1},{1}));//I->F
  staticRoutingI->AddHostRouteTo (iBD.GetAddress (0,0), iFI.GetAddress (1,0), rvector ({1},{1}));//I->F
  staticRoutingF->AddHostRouteTo (iBD.GetAddress (0,0), iFI.GetAddress (1,0), rvector ({2},{1}));//F->D
  staticRoutingD->AddHostRouteTo (iBD.GetAddress (0,0), iFI.GetAddress (1,0), rvector ({1},{1}));//D->B

  staticRoutingI->AddHostRouteTo (iCE.GetAddress (0,0), fromLocal, rvector ({1},{1}));//I->F
  staticRoutingI->AddHostRouteTo (iCE.GetAddress (0,0), iFI.GetAddress (1,0), rvector ({1},{1}));//I->F
  staticRoutingF->AddHostRouteTo (iCE.GetAddress (0,0), iFI.GetAddress (1,0), rvector ({1},{1}));//F->E
  staticRoutingE->AddHostRouteTo (iCE.GetAddress (0,0), iFI.GetAddress (1,0), rvector ({1},{1}));//E->C

  staticRoutingI->AddHostRouteTo (iDF.GetAddress (0,0), fromLocal, rvector ({1},{1}));//I->F
  staticRoutingI->AddHostRouteTo (iDF.GetAddress (0,0), iFI.GetAddress (1,0), rvector ({1},{1}));//I->F
  staticRoutingF->AddHostRouteTo (iDF.GetAddress (0,0), iFI.GetAddress (1,0), rvector ({2},{1}));//F->D

  staticRoutingI->AddHostRouteTo (iEH.GetAddress (0,0), fromLocal, rvector ({2},{1}));//I->H
  staticRoutingI->AddHostRouteTo (iEH.GetAddress (0,0), iHI.GetAddress (1,0), rvector ({2},{1}));//I->H
  staticRoutingH->AddHostRouteTo (iEH.GetAddress (0,0), iHI.GetAddress (1,0), rvector ({1},{1}));//H->E

  staticRoutingI->AddHostRouteTo (iFI.GetAddress (0,0), fromLocal, rvector ({1},{1}));//I->F
  staticRoutingI->AddHostRouteTo (iFI.GetAddress (0,0), iFI.GetAddress (1,0), rvector ({1},{1}));//I->F

  staticRoutingI->AddHostRouteTo (iGH.GetAddress (0,0), fromLocal, rvector ({2},{1}));//I->H
  staticRoutingI->AddHostRouteTo (iGH.GetAddress (0,0), iHI.GetAddress (1,0), rvector ({2},{1}));//I->H
  staticRoutingH->AddHostRouteTo (iGH.GetAddress (0,0), iHI.GetAddress (1,0), rvector ({2},{1}));//H->G

  staticRoutingI->AddHostRouteTo (iHI.GetAddress (0,0), fromLocal, rvector ({2},{1}));//I->H
  staticRoutingI->AddHostRouteTo (iHI.GetAddress (0,0), iHI.GetAddress (1,0), rvector ({2},{1}));//I->H

  staticRoutingI->AddHostRouteTo (iIJ.GetAddress (1,0), fromLocal, rvector ({3},{1}));//I->J
  staticRoutingI->AddHostRouteTo (iIJ.GetAddress (1,0), iIJ.GetAddress (0,0), rvector ({3},{1}));//I->J

  staticRoutingI->AddHostRouteTo (iJK.GetAddress (1,0), fromLocal, rvector ({3},{1}));//I->J
  staticRoutingI->AddHostRouteTo (iJK.GetAddress (1,0), iIJ.GetAddress (0,0), rvector ({3},{1}));//I->J
  staticRoutingJ->AddHostRouteTo (iJK.GetAddress (1,0), iIJ.GetAddress (0,0), rvector ({2},{1}));//J->K

  staticRoutingJ->AddHostRouteTo (iAC.GetAddress (0,0), fromLocal, rvector ({1},{1}));//J->I
  staticRoutingJ->AddHostRouteTo (iAC.GetAddress (0,0), iIJ.GetAddress (1,0), rvector ({1},{1}));//J->I
  staticRoutingI->AddHostRouteTo (iAC.GetAddress (0,0), iIJ.GetAddress (1,0), rvector ({1},{1}));//I->F
  staticRoutingF->AddHostRouteTo (iAC.GetAddress (0,0), iIJ.GetAddress (1,0), rvector ({1},{1}));//F->E
  staticRoutingE->AddHostRouteTo (iAC.GetAddress (0,0), iIJ.GetAddress (1,0), rvector ({1},{1}));//E->C
  staticRoutingC->AddHostRouteTo (iAC.GetAddress (0,0), iIJ.GetAddress (1,0), rvector ({1},{1}));//C->A

  staticRoutingJ->AddHostRouteTo (iBD.GetAddress (0,0), fromLocal, rvector ({1},{1}));//J->I
  staticRoutingJ->AddHostRouteTo (iBD.GetAddress (0,0), iIJ.GetAddress (1,0), rvector ({1},{1}));//J->I
  staticRoutingI->AddHostRouteTo (iBD.GetAddress (0,0), iIJ.GetAddress (1,0), rvector ({1},{1}));//I->F
  staticRoutingF->AddHostRouteTo (iBD.GetAddress (0,0), iIJ.GetAddress (1,0), rvector ({2},{1}));//F->D
  staticRoutingD->AddHostRouteTo (iBD.GetAddress (0,0), iIJ.GetAddress (1,0), rvector ({1},{1}));//D->B

  staticRoutingJ->AddHostRouteTo (iCE.GetAddress (0,0), fromLocal, rvector ({1},{1}));//J->I
  staticRoutingJ->AddHostRouteTo (iCE.GetAddress (0,0), iIJ.GetAddress (1,0), rvector ({1},{1}));//J->I
  staticRoutingI->AddHostRouteTo (iCE.GetAddress (0,0), iIJ.GetAddress (1,0), rvector ({1},{1}));//I->F
  staticRoutingF->AddHostRouteTo (iCE.GetAddress (0,0), iIJ.GetAddress (1,0), rvector ({1},{1}));//F->E
  staticRoutingE->AddHostRouteTo (iCE.GetAddress (0,0), iIJ.GetAddress (1,0), rvector ({1},{1}));//E->C

  staticRoutingJ->AddHostRouteTo (iCE.GetAddress (0,0), fromLocal, rvector ({2},{1}));//J->K
  staticRoutingJ->AddHostRouteTo (iCE.GetAddress (0,0), iJK.GetAddress (0,0), rvector ({2},{1}));//J->K
  staticRoutingK->AddHostRouteTo (iCE.GetAddress (0,0), iJK.GetAddress (0,0), rvector ({1},{1}));//K->G
  staticRoutingG->AddHostRouteTo (iCE.GetAddress (0,0), iJK.GetAddress (0,0), rvector ({1},{1}));//G->H
  staticRoutingH->AddHostRouteTo (iCE.GetAddress (0,0), iJK.GetAddress (0,0), rvector ({1},{1}));//H->E
  staticRoutingE->AddHostRouteTo (iCE.GetAddress (0,0), iJK.GetAddress (0,0), rvector ({1},{1}));//E->C

  staticRoutingJ->AddHostRouteTo (iDF.GetAddress (0,0), fromLocal, rvector ({1},{1}));//J->I
  staticRoutingJ->AddHostRouteTo (iDF.GetAddress (0,0), iIJ.GetAddress (1,0), rvector ({1},{1}));//J->I
  staticRoutingI->AddHostRouteTo (iDF.GetAddress (0,0), iIJ.GetAddress (1,0), rvector ({1},{1}));//I->F
  staticRoutingF->AddHostRouteTo (iDF.GetAddress (0,0), iIJ.GetAddress (1,0), rvector ({2},{1}));//F->D

  staticRoutingJ->AddHostRouteTo (iEH.GetAddress (0,0), fromLocal, rvector ({1},{1}));//J->I
  staticRoutingJ->AddHostRouteTo (iEH.GetAddress (0,0), iIJ.GetAddress (1,0), rvector ({1},{1}));//J->I
  staticRoutingI->AddHostRouteTo (iEH.GetAddress (0,0), iIJ.GetAddress (1,0), rvector ({2},{1}));//I->H
  staticRoutingH->AddHostRouteTo (iEH.GetAddress (0,0), iIJ.GetAddress (1,0), rvector ({1},{1}));//H->E

  staticRoutingJ->AddHostRouteTo (iFI.GetAddress (0,0), fromLocal, rvector ({1},{1}));//J->I
  staticRoutingJ->AddHostRouteTo (iFI.GetAddress (0,0), iIJ.GetAddress (1,0), rvector ({1},{1}));//J->I
  staticRoutingI->AddHostRouteTo (iFI.GetAddress (0,0), iIJ.GetAddress (1,0), rvector ({1},{1}));//I->F

  staticRoutingJ->AddHostRouteTo (iGK.GetAddress (0,0), fromLocal, rvector ({2},{1}));//J->K
  staticRoutingJ->AddHostRouteTo (iGK.GetAddress (0,0), iJK.GetAddress (0,0), rvector ({2},{1}));//J->K
  staticRoutingK->AddHostRouteTo (iGK.GetAddress (0,0), iJK.GetAddress (0,0), rvector ({1},{1}));//K->G

  staticRoutingJ->AddHostRouteTo (iGH.GetAddress (1,0), fromLocal, rvector ({2},{1}));//J->K
  staticRoutingJ->AddHostRouteTo (iGH.GetAddress (1,0), iJK.GetAddress (0,0), rvector ({2},{1}));//J->K
  staticRoutingK->AddHostRouteTo (iGH.GetAddress (1,0), iJK.GetAddress (0,0), rvector ({1},{1}));//K->G
  staticRoutingG->AddHostRouteTo (iGH.GetAddress (1,0), iJK.GetAddress (0,0), rvector ({1},{1}));//G->H

  staticRoutingJ->AddHostRouteTo (iIJ.GetAddress (0,0), fromLocal, rvector ({1},{1}));//J->I
  staticRoutingJ->AddHostRouteTo (iIJ.GetAddress (0,0), iIJ.GetAddress (1,0), rvector ({1},{1}));//J->I

  staticRoutingJ->AddHostRouteTo (iJK.GetAddress (1,0), fromLocal, rvector ({2},{1}));//J->K
  staticRoutingJ->AddHostRouteTo (iJK.GetAddress (1,0), iJK.GetAddress (0,0), rvector ({2},{1}));//J->K

  staticRoutingK->AddHostRouteTo (iAC.GetAddress (0,0), fromLocal, rvector ({1},{1}));//K->G
  staticRoutingK->AddHostRouteTo (iAC.GetAddress (0,0), iGK.GetAddress (1,0), rvector ({1},{1}));//K->G
  staticRoutingG->AddHostRouteTo (iAC.GetAddress (0,0), iGK.GetAddress (1,0), rvector ({1},{1}));//G->H
  staticRoutingH->AddHostRouteTo (iAC.GetAddress (0,0), iGK.GetAddress (1,0), rvector ({1},{1}));//H->E
  staticRoutingE->AddHostRouteTo (iAC.GetAddress (0,0), iGK.GetAddress (1,0), rvector ({1},{1}));//E->C
  staticRoutingC->AddHostRouteTo (iAC.GetAddress (0,0), iGK.GetAddress (1,0), rvector ({1},{1}));//C->A

  staticRoutingK->AddHostRouteTo (iBC.GetAddress (0,0), fromLocal, rvector ({1},{1}));//K->G
  staticRoutingK->AddHostRouteTo (iBC.GetAddress (0,0), iGK.GetAddress (1,0), rvector ({1},{1}));//K->G
  staticRoutingG->AddHostRouteTo (iBC.GetAddress (0,0), iGK.GetAddress (1,0), rvector ({1},{1}));//G->H
  staticRoutingH->AddHostRouteTo (iBC.GetAddress (0,0), iGK.GetAddress (1,0), rvector ({1},{1}));//H->E
  staticRoutingE->AddHostRouteTo (iBC.GetAddress (0,0), iGK.GetAddress (1,0), rvector ({1},{1}));//E->C
  staticRoutingC->AddHostRouteTo (iBC.GetAddress (0,0), iGK.GetAddress (1,0), rvector ({2},{1}));//C->B

  staticRoutingK->AddHostRouteTo (iCE.GetAddress (0,0), fromLocal, rvector ({1},{1}));//K->G
  staticRoutingK->AddHostRouteTo (iCE.GetAddress (0,0), iGK.GetAddress (1,0), rvector ({1},{1}));//K->G
  staticRoutingG->AddHostRouteTo (iCE.GetAddress (0,0), iGK.GetAddress (1,0), rvector ({1},{1}));//G->H
  staticRoutingH->AddHostRouteTo (iCE.GetAddress (0,0), iGK.GetAddress (1,0), rvector ({1},{1}));//H->E
  staticRoutingE->AddHostRouteTo (iCE.GetAddress (0,0), iGK.GetAddress (1,0), rvector ({1},{1}));//E->C

  staticRoutingK->AddHostRouteTo (iDF.GetAddress (0,0), fromLocal, rvector ({2},{1}));//K->J
  staticRoutingK->AddHostRouteTo (iDF.GetAddress (0,0), iJK.GetAddress (1,0), rvector ({2},{1}));//K->J
  staticRoutingJ->AddHostRouteTo (iDF.GetAddress (0,0), iJK.GetAddress (1,0), rvector ({1},{1}));//J->I
  staticRoutingI->AddHostRouteTo (iDF.GetAddress (0,0), iJK.GetAddress (1,0), rvector ({1},{1}));//I->F
  staticRoutingF->AddHostRouteTo (iDF.GetAddress (0,0), iJK.GetAddress (1,0), rvector ({2},{1}));//F->D

  staticRoutingK->AddHostRouteTo (iEH.GetAddress (0,0), fromLocal, rvector ({1},{1}));//K->G
  staticRoutingK->AddHostRouteTo (iEH.GetAddress (0,0), iGK.GetAddress (1,0), rvector ({1},{1}));//K->G
  staticRoutingG->AddHostRouteTo (iEH.GetAddress (0,0), iGK.GetAddress (1,0), rvector ({1},{1}));//G->H
  staticRoutingH->AddHostRouteTo (iEH.GetAddress (0,0), iGK.GetAddress (1,0), rvector ({1},{1}));//H->E

  staticRoutingK->AddHostRouteTo (iFI.GetAddress (0,0), fromLocal, rvector ({2},{1}));//K->J
  staticRoutingK->AddHostRouteTo (iFI.GetAddress (0,0), iJK.GetAddress (1,0), rvector ({2},{1}));//K->J
  staticRoutingJ->AddHostRouteTo (iFI.GetAddress (0,0), iJK.GetAddress (1,0), rvector ({1},{1}));//J->I
  staticRoutingI->AddHostRouteTo (iFI.GetAddress (0,0), iJK.GetAddress (1,0), rvector ({1},{1}));//I->F

  staticRoutingK->AddHostRouteTo (iGK.GetAddress (0,0), fromLocal, rvector ({1},{1}));//K->G
  staticRoutingK->AddHostRouteTo (iGK.GetAddress (0,0), iGK.GetAddress (1,0), rvector ({1},{1}));//K->G

  staticRoutingK->AddHostRouteTo (iGH.GetAddress (1,0), fromLocal, rvector ({1},{1}));//K->G
  staticRoutingK->AddHostRouteTo (iGH.GetAddress (1,0), iGK.GetAddress (1,0), rvector ({1},{1}));//K->G
  staticRoutingG->AddHostRouteTo (iGH.GetAddress (1,0), iGK.GetAddress (1,0), rvector ({1},{1}));//G->H

  staticRoutingK->AddHostRouteTo (iHI.GetAddress (1,0), fromLocal, rvector ({1},{1}));//K->G
  staticRoutingK->AddHostRouteTo (iHI.GetAddress (1,0), iGK.GetAddress (1,0), rvector ({1},{1}));//K->G
  staticRoutingG->AddHostRouteTo (iHI.GetAddress (1,0), iGK.GetAddress (1,0), rvector ({1},{1}));//G->H
  staticRoutingH->AddHostRouteTo (iHI.GetAddress (1,0), iGK.GetAddress (1,0), rvector ({3},{1}));//H->I

  staticRoutingK->AddHostRouteTo (iJK.GetAddress (0,0), fromLocal, rvector ({2},{1}));//K->J
  staticRoutingK->AddHostRouteTo (iJK.GetAddress (0,0), iJK.GetAddress (1,0), rvector ({2},{1}));//K->J

  //Install sink App
    uint16_t sinkPort = 9;
    PacketSinkHelper packetSinkHelper ("ns3::TcpSocketFactory", InetSocketAddress (Ipv4Address::GetAny (), sinkPort));
    std::array<ApplicationContainer, 11> sinkApps;
    for(int i = 0; i <= 10; i++){
      sinkApps[i] = packetSinkHelper.Install (c.Get (i));
      sinkApps[i].Start (Seconds (0.));
      sinkApps[i].Stop (Seconds (10));
    }
  //

  // Setup source application
      TypeId tid = TypeId::LookupByName ("ns3::TcpSocketFactory");
      // for (int i = 0; i <= 0; i++)
      // {
      //   for (int j = 0; j <= 0; j++)
      //   {
      //     if (j != i)
      //     {
      //       Ptr<MyApp> app = CreateObject<MyApp> ();
      //       Ptr<Node> node = c.Get (i);
      //       Address sinkAddress = iAB.GetAddress(1);
      //       app->Setup (tid, node ,sinkAddress, 1300, 10, DataRate ("5Mbps"), "n" + std::to_string(i) + "-n" + std::to_string(j));
      //       node->AddApplication (app);
      //       app->SetStartTime (Seconds (0));
      //       app->SetStopTime (Seconds (10));
      //     }
      //   }
      // }
    
    Ptr<MyApp> app = CreateObject<MyApp> ();
    Ptr<Node> installTo = c.Get (10);
    Address sinkAddress = InetSocketAddress (iAC.GetAddress (0), sinkPort);
    app->Setup (tid, installTo ,sinkAddress, 1300, 50, DataRate ("500Kbps"), "A->B");
    installTo->AddApplication (app);
    app->SetStartTime (Seconds (0));
    app->SetStopTime (Seconds (10));
  
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
    AnimationInterface anim ("./Data/static-route-c.xml");
  //Animation settings end

 AsciiTraceHelper ascii;
  Simulator::Stop (Seconds (10));
  Simulator::Run ();
  Simulator::Destroy ();
  return 0;
}