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
#define ONE_DATUM 100 //パケットで1コネクション
#define DEFAULT_SEND_RATE "0.5Mbps"
#define NUM_PACKETS 3000000
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
  // m_cwndStream = ascii.CreateFileStream ("./Data"+m_name+".cwnd");
  // m_datarateStream = ascii.CreateFileStream ("./Data"+m_name+".drate");
  m_lossStream = ascii.CreateFileStream ("./Data"+m_name+".loss");
  m_tcpTxStream = ascii.CreateFileStream ("./Data"+m_name+".thr");
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
    if (m_tcpsent != 0)
    {
      double lossRate = m_packetLoss / (double) m_tcpsent;
      ChangeDataRate (lossRate);
    }
    // Trace datarate, lossrate
    // *m_datarateStream->GetStream () << Simulator::Now ().GetSeconds () << " " << m_dataRate.GetBitRate () << std::endl;
    
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
    m_tcpsentSize += packet->GetSize () * 8;//bits
  }
}

void
MyApp::woTCPTx (double time)
{
  *m_tcpTxStream->GetStream () << Simulator::Now ().GetSeconds () << " " << m_tcpsentSize / time << std::endl;
  if (m_tcpsentCount != 0 && m_packetLossParTime != 0)
  {
    *m_lossStream->GetStream () << Simulator::Now ().GetSeconds () << " " << m_packetLossParTime / (double) m_tcpsentCount  << std::endl;
    
  } else {
    *m_lossStream->GetStream () << Simulator::Now ().GetSeconds () << " " << 0 << std::endl;
  }
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

  int originNode = 0;
  int destinationNode = 10;
  cmd.AddValue ("OrigNode", "Origin node increase flow", originNode);
  cmd.AddValue ("DestNode", "Destination node increase flow", destinationNode);
  cmd.Parse (argc, argv);

  // Set default tcp type
  Config::SetDefault ("ns3::TcpL4Protocol::SocketType", StringValue (TCP_TYPE));
  // Set default tcp segment size
  Config::SetDefault ("ns3::TcpSocket::SegmentSize", UintegerValue (SEGMENT_SIZE)); 

  srand((unsigned)time(NULL));

///INSERT///

  // Setup sink App

    std::array<ApplicationContainer, 11*10> sinkApps;
    for(int i = 0; i <= 10; i++){
      for (int j = 1; j <= 10; j++)
      {
        PacketSinkHelper packetSinkHelper ("ns3::TcpSocketFactory", InetSocketAddress (Ipv4Address::GetAny (), j));
        sinkApps[i*10+j-1] = packetSinkHelper.Install (c_e.Get (i));
        sinkApps[i*10+j-1].Start (Seconds (0.));
        sinkApps[i*10+j-1].Stop (Seconds (END_TIME));
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
          for (int k = 1; k <= 10; k++)
          {
            Ptr<MyApp> app = CreateObject<MyApp> ();
            Ptr<Node> node = c_e.Get (i);
            Address sinkAddress = InetSocketAddress (sinkAddresses[j], k);
            if (i == originNode && j == destinationNode)
            {
              app->Setup (tid, node ,sinkAddress, PACKET_SIZE, NUM_PACKETS, DataRate ("3.5Mbps"), "n" + std::to_string(i) + "-n" + std::to_string(j)+"-p"+std::to_string(k));
            } else {
              app->Setup (tid, node ,sinkAddress, PACKET_SIZE, NUM_PACKETS, DataRate (DEFAULT_SEND_RATE), "n" + std::to_string(i) + "-n" + std::to_string(j)+"-p"+std::to_string(k));
            }
            node->AddApplication (app);
            app->SetStartTime (Seconds (0));
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

  Simulator::Stop (Seconds (END_TIME));
  Simulator::Run ();
  Simulator::Destroy ();
  return 0; 
}