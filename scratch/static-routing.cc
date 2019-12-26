#include <iostream>
#include <fstream>
#include <string>
#include <cassert>
#include <stdlib.h>
#include <time.h>

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

void ChangeRoute (std::tuple<Ptr<Ipv4StaticRouting>, Ipv4Address, int, double> arg, Ptr<ns3::Packet const> pkt)
{
  int i;
  for (i = 0; i < std::get<0>(arg)->GetNRoutes (); i++)
  {
    if (std::get<0>(arg)->GetRoute (i).IsHost () && std::get<0>(arg)->GetRoute (i).GetDest () == std::get<1>(arg))
    {
      if ( (double)rand()/RAND_MAX <= 1 - std::get<3>(arg) ) {
        std::get<0>(arg)->AddHostRouteTo (std::get<0>(arg)->GetRoute (i).GetDest (), std::get<2>(arg));
        std::get<0>(arg)->RemoveRoute (i);
      }
      break;
    }
  }
}


int 
main (int argc, char *argv[])
{

  // Allow the user to override any of the defaults and the above
  // DefaultValue::Bind ()s at run-time, via command-line arguments
  CommandLine cmd;
  cmd.Parse (argc, argv);

  srand((unsigned)time(NULL));

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

  // Later, we add IP addresses.
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

  Ptr<Ipv4> ipv4A = c.Get (0)->GetObject<Ipv4> ();
  Ptr<Ipv4> ipv4B = c.Get (1)->GetObject<Ipv4> ();
  Ptr<Ipv4> ipv4C = c.Get (2)->GetObject<Ipv4> ();
  Ptr<Ipv4> ipv4D = c.Get (3)->GetObject<Ipv4> ();
  Ptr<Ipv4> ipv4G = c.Get (6)->GetObject<Ipv4> ();

  std::cout << ipv4A->GetAddress (2,0).GetLocal () << std::endl; 
  Ipv4StaticRoutingHelper ipv4RoutingHelper;
  Ptr<Ipv4StaticRouting> staticRoutingA = ipv4RoutingHelper.GetStaticRouting (ipv4A);
  Ptr<Ipv4StaticRouting> staticRoutingB = ipv4RoutingHelper.GetStaticRouting (ipv4B);
  Ptr<Ipv4StaticRouting> staticRoutingC = ipv4RoutingHelper.GetStaticRouting (ipv4C);
  Ptr<Ipv4StaticRouting> staticRoutingD = ipv4RoutingHelper.GetStaticRouting (ipv4D);
  Ptr<Ipv4StaticRouting> staticRoutingG = ipv4RoutingHelper.GetStaticRouting (ipv4G);
  
  // // Create static routes from A to F (G->A->D->F) ///TEST
  staticRoutingG->AddHostRouteTo (Ipv4Address ("10.1.7.2"), 1);
  staticRoutingA->AddHostRouteTo (Ipv4Address ("10.1.7.2"), 3);
  staticRoutingD->AddHostRouteTo (Ipv4Address ("10.1.7.2"), 3);
  // //////

  // Create static routes from A to F (A->C->F)
  staticRoutingA->AddHostRouteTo (Ipv4Address ("10.1.7.2"), 2);
  staticRoutingC->AddHostRouteTo (Ipv4Address ("10.1.7.2"), 3);
  //////

  // Create static routes from A to E (A->B->E, A->D->E)
  staticRoutingA->AddHostRouteTo (Ipv4Address ("10.1.8.2"), 1);
  staticRoutingB->AddHostRouteTo (Ipv4Address ("10.1.8.2"), 2);
  staticRoutingD->AddHostRouteTo (Ipv4Address ("10.1.8.2"), 2);
  std::tuple<Ptr<Ipv4StaticRouting>, Ipv4Address, int, double> argA, argA_2;
  argA = std::tuple<Ptr<Ipv4StaticRouting>, Ipv4Address, int, double>(staticRoutingA, Ipv4Address ("10.1.8.2"), 3, 0.7);
  argA_2 = std::tuple<Ptr<Ipv4StaticRouting>, Ipv4Address, int, double>(staticRoutingA, Ipv4Address ("10.1.8.2"), 1, 0.3);
  dAdB.Get (0)->TraceConnectWithoutContext("PhyTxEnd", MakeBoundCallback(&ChangeRoute, argA));
  dAdD.Get (0)->TraceConnectWithoutContext("PhyTxEnd", MakeBoundCallback(&ChangeRoute, argA_2));
  //////

  uint16_t port = 9;
  OnOffHelper onoff ("ns3::UdpSocketFactory", 
                          InetSocketAddress ("10.1.7.2", port));
  onoff.SetConstantRate (DataRate (10000));
  ApplicationContainer sourceApps = onoff.Install (c.Get (0));
  sourceApps.Start (Seconds (0.0));
  sourceApps.Stop (Seconds (10.0));

  OnOffHelper onoff2 ("ns3::UdpSocketFactory", 
                          InetSocketAddress ("10.1.8.2", port));
  onoff2.SetConstantRate (DataRate (10000));
  ApplicationContainer sourceApps2 = onoff2.Install (c.Get (0));
  sourceApps2.Start (Seconds (0.0));
  sourceApps2.Stop (Seconds (10.0));

  OnOffHelper onoff3 ("ns3::UdpSocketFactory", 
                          InetSocketAddress ("10.1.7.2", port));
  onoff3.SetConstantRate (DataRate (20000));
  ApplicationContainer sourceApps3 = onoff3.Install (c.Get (6));
  sourceApps3.Start (Seconds (0.0));
  sourceApps3.Stop (Seconds (10.0));

// Create a packet sink to receive these packets
  PacketSinkHelper sink ("ns3::UdpSocketFactory",
                        Address (InetSocketAddress (Ipv4Address::GetAny (), port)));
  ApplicationContainer apps = sink.Install(c.Get (5));
  apps.Start (Seconds (1.0));
  apps.Stop (Seconds (10.0));

  PacketSinkHelper sink2 ("ns3::UdpSocketFactory",
                        Address (InetSocketAddress (Ipv4Address::GetAny (), port)));
  ApplicationContainer apps2 = sink2.Install(c.Get (4));
  apps2.Start (Seconds (1.0));
  apps2.Stop (Seconds (10.0));
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