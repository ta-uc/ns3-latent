// Test program for this 3-router scenario, using static routing
//
// (a.a.a.a/32)A <-- x.x.x.0/30 --> B <-- y.y.y.0/30 --> C(c.c.c.c/32)

#include <iostream>
#include <fstream>
#include <string>
#include <cassert>

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/csma-module.h"
#include "ns3/applications-module.h"
#include "ns3/ipv4-static-routing-helper.h"
#include "ns3/netanim-module.h"


using namespace ns3;

NS_LOG_COMPONENT_DEFINE ("StaticRoutingSlash32Test");

void ChangeRoute (Ptr<Ipv4StaticRouting> rt, int mode)
{
  rt->RemoveRoute(3);
  if (mode)
  {
    rt->AddHostRouteTo (Ipv4Address ("10.1.4.2"), Ipv4Address ("10.1.3.2"), 2);
  }else
  {
    rt->AddHostRouteTo (Ipv4Address ("10.1.4.2"), Ipv4Address ("10.1.1.2"), 1);
  }
}

int 
main (int argc, char *argv[])
{

  // Allow the user to override any of the defaults and the above
  // DefaultValue::Bind ()s at run-time, via command-line arguments
  CommandLine cmd;
  cmd.Parse (argc, argv);

  Ptr<Node> nA = CreateObject<Node> ();
  Ptr<Node> nB = CreateObject<Node> ();
  Ptr<Node> nC = CreateObject<Node> ();
  Ptr<Node> nD = CreateObject<Node> ();

  NodeContainer c = NodeContainer (nA, nB, nC, nD);

  InternetStackHelper internet;
  internet.Install (c);

  // Point-to-point links
  NodeContainer nAnB = NodeContainer (nA, nB);
  NodeContainer nAnC = NodeContainer (nA, nC);
  NodeContainer nBnD = NodeContainer (nB, nD);
  NodeContainer nCnD = NodeContainer (nC, nD);

  // We create the channels first without any IP addressing information
  PointToPointHelper p2p;
  p2p.SetDeviceAttribute ("DataRate", StringValue ("5Mbps"));
  p2p.SetChannelAttribute ("Delay", StringValue ("2ms"));
  NetDeviceContainer dAdB = p2p.Install (nAnB);
  NetDeviceContainer dAdC = p2p.Install (nAnC);
  NetDeviceContainer dBdD = p2p.Install (nBnD);
  NetDeviceContainer dCdD = p2p.Install (nCnD);

  // Later, we add IP addresses.
  Ipv4AddressHelper ipv4;
  ipv4.SetBase ("10.1.1.0", "255.255.255.0");
  Ipv4InterfaceContainer iAiB = ipv4.Assign (dAdB);

  ipv4.SetBase ("10.1.2.0", "255.255.255.0");
  Ipv4InterfaceContainer iBiD = ipv4.Assign (dBdD);

  ipv4.SetBase ("10.1.3.0", "255.255.255.0");
  Ipv4InterfaceContainer iAiC = ipv4.Assign (dAdC);

  ipv4.SetBase ("10.1.4.0", "255.255.255.0");
  Ipv4InterfaceContainer iCiD = ipv4.Assign (dCdD);

  Ptr<Ipv4> ipv4A = nA->GetObject<Ipv4> ();
  Ptr<Ipv4> ipv4B = nB->GetObject<Ipv4> ();
  Ptr<Ipv4> ipv4C = nC->GetObject<Ipv4> ();
  Ptr<Ipv4> ipv4D = nD->GetObject<Ipv4> ();

  Ipv4StaticRoutingHelper ipv4RoutingHelper;
  // Create static routes from A to D
  
  Ptr<Ipv4StaticRouting> staticRoutingA = ipv4RoutingHelper.GetStaticRouting (ipv4A);
  // The ifIndex for this outbound route is 1; the first p2p link added
  staticRoutingA->AddHostRouteTo (Ipv4Address ("10.1.4.2"), Ipv4Address ("10.1.1.2"), 1);

  bool b = 1;
  for (int i = 1; i < 10; i++)
  {
    Simulator::Schedule (Time ( Seconds (i)), &ChangeRoute, staticRoutingA, b);
    b = !b;
  }
  
  Ptr<Ipv4StaticRouting> staticRoutingB = ipv4RoutingHelper.GetStaticRouting (ipv4B);
  // The ifIndex we want on node B is 2; 0 corresponds to loopback, and 1 to the first point to point link
  staticRoutingB->AddHostRouteTo (Ipv4Address ("10.1.4.2"), Ipv4Address ("10.1.2.2"), 2);
  
  Ptr<Ipv4StaticRouting> staticRoutingC = ipv4RoutingHelper.GetStaticRouting (ipv4C);
  staticRoutingC->AddHostRouteTo (Ipv4Address ("10.1.4.2"), Ipv4Address ("10.1.4.2"), 2);

  // Create the OnOff application to send UDP datagrams of size
  // 210 bytes at a rate of 448 Kb/s
  uint16_t port = 9;   // Discard port (RFC 863)
  OnOffHelper onoff ("ns3::UdpSocketFactory", 
                      Address (InetSocketAddress (Ipv4Address("10.1.4.2"), port)));
  onoff.SetConstantRate (DataRate (10000));
  ApplicationContainer apps = onoff.Install (nA);
  apps.Start (Seconds (1.0));
  apps.Stop (Seconds (10.0));

  // Create a packet sink to receive these packets
  PacketSinkHelper sink ("ns3::UdpSocketFactory",
                        Address (InetSocketAddress (Ipv4Address::GetAny (), port)));
  apps = sink.Install (nD);
  apps.Start (Seconds (1.0));
  apps.Stop (Seconds (10.0));

// Animation settings
  AnimationInterface::SetConstantPosition (c.Get (0),0.0,2.0);
  AnimationInterface::SetConstantPosition (c.Get (1),2.0,1.0);
  AnimationInterface::SetConstantPosition (c.Get (2),2.0,3.0);
  AnimationInterface::SetConstantPosition (c.Get (3),4.0,2.0);
  AnimationInterface anim ("./Data/static-route.xml");
//Animation settings end

//  AsciiTraceHelper ascii;
//  p2p.EnableAsciiAll (ascii.CreateFileStream ("static-routing-slash32.tr"));
//  p2p.EnablePcapAll ("static-routing-slash32");

  Simulator::Run ();
  Simulator::Destroy ();

  return 0;
}