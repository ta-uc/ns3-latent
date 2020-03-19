from topology import nodes, links, links_all
from opt_route_capa import opt_route_capa
import argparse

parser = argparse.ArgumentParser(description='Write out NS3 program')
parser.add_argument('--OrigNode', action="store", dest="orig_node", default="A")
parser.add_argument('--DestNode', action="store", dest="dest_node", default="K")
parser.add_argument('--OdRate', action="store", dest="od_rate", default="")
parser.add_argument('--Opt', action="store_true")
results = parser.parse_args()

opt_route_capa(results.orig_node,results.dest_node,results.od_rate)
if results.Opt:
  from capas_incd import capas
else:
  from topology import capas

print("""  NodeContainer c, c_e;
  c.Create ({0});
  c_e.Create ({0});

  InternetStackHelper internet;
  internet.Install (c);
  internet.Install (c_e);
""".format(
len(nodes)
))

#point-to-point
for link in links:
  print(
  """  NodeContainer n{} = NodeContainer (c.Get ({}), c.Get ({}));"""
  .format(
    link,
    nodes.index(link[0]),
    nodes.index(link[1])
  ))

print("")

for node in nodes:
  print(
  """  NodeContainer n{0}{0}e = NodeContainer (c.Get ({1}), c_e.Get ({1}));"""
  .format(
    node,
    nodes.index(node)
  ))
print("")

print(
  """  PointToPointHelper p2p, p2p_l;
  p2p.SetChannelAttribute ("Delay", StringValue ("1ms"));
  p2p_l.SetDeviceAttribute ("DataRate", StringValue ("300Mbps"));
  """)

for link in links:
  print(
  """  NetDeviceContainer d{0} = p2p.Install (n{0});"""
  .format(link))

print("")

for node in nodes:
  print(
  """  NetDeviceContainer d{0}{0}e = p2p_l.Install (n{0}{0}e);"""
  .format(node))

print("")


for node_num in range(len(nodes)):
  part_links = [link for link in links_all if link[0] == nodes[node_num]]
  for part_link_num in range(len(part_links)):
    print("""  Config::Set("/NodeList/{0}/$ns3::Node/DeviceList/{1}/$ns3::PointToPointNetDevice/DataRate", DataRateValue (DataRate("{2}Mbps")));"""
    .format(
      node_num,
      part_link_num + 1,
      capas[part_links[part_link_num]]
    ))

print("")
print("""  TrafficControlHelper tch;
  tch.SetRootQueueDisc ("ns3::FqCoDelQueueDisc");""")

for link in links:
  print("""  tch.Install (d{0});"""
  .format(
    link
  ))

print("")

k = 0
for node_num in range(len(nodes)):
  part_links = [link for link in links_all if link[0] == nodes[node_num]]
  for part_link_num in range(len(part_links)):
    if nodes[node_num]+part_links[part_link_num][1] in links:
      d = nodes[node_num]+part_links[part_link_num][1]
      n = 0
    else:
      d = part_links[part_link_num][1]+nodes[node_num]
      n = 1
    print("""  d{0}.Get ({1})->TraceConnectWithoutContext("PhyTxEnd", MakeBoundCallback(&linkPktCount, {2}));"""
    .format(
      d,
      n,
      k
    ))
    k += 1

print("")
l = 0
for node_num in range(len(nodes)):
  part_links = [link for link in links_all if link[0] == nodes[node_num]]
  for part_link_num in range(len(part_links)):
    print("""  Config::ConnectWithoutContext ("/NodeList/{0}/$ns3::TrafficControlLayer/RootQueueDiscList/{1}/Drop", MakeBoundCallback (&linkPktLossCount, {2}));"""
    .format(
      node_num,
      part_link_num + 1,
      l
    ))
    l += 1

print("")
print("  Ipv4AddressHelper ipv4;")

i = 1
for link in links:
  print(
  """  ipv4.SetBase ("10.1.{0}.0", "255.255.255.0");
  Ipv4InterfaceContainer i{1} = ipv4.Assign (d{1});"""
  .format(
    i,
    link
  ))
  i += 1

print("")
print("  std::vector <ns3::Ipv4Address> sinkAddresses;")

for node in nodes:
  print("""  ipv4.SetBase ("192.168.{0}.0", "255.255.255.0");
  Ipv4InterfaceContainer i{1}{1}e = ipv4.Assign (d{1}{1}e);
  sinkAddresses.push_back(i{1}{1}e.GetAddress(1));"""
  .format(
    nodes.index(node)+1,
    node
  ))

print("")

for node in nodes:
  print("""  Ptr<Ipv4> ipv4{0} = c.Get ({1})->GetObject<Ipv4> ();"""
  .format(
    node,
    nodes.index(node)))

print("")

for node in nodes:
  print("""  Ptr<Ipv4> ipv4{0}e = c_e.Get ({1})->GetObject<Ipv4> ();"""
  .format(
    node,
    nodes.index(node)))

print("")
print("  Ipv4StaticRoutingHelper ipv4RoutingHelper;")

for node in nodes:
  print(
    """  Ptr<Ipv4StaticRouting> staticRouting{0} = ipv4RoutingHelper.GetStaticRouting (ipv4{0});"""
    .format(
      node
    ))

print("")

for node in nodes:
  print(
    """  Ptr<Ipv4StaticRouting> staticRouting{0}e = ipv4RoutingHelper.GetStaticRouting (ipv4{0}e);"""
    .format(
      node
    ))

print("")
print("""  Ipv4Address fromLocal = Ipv4Address ("102.102.102.102");""")
print("")

for node0 in nodes:
  for node1 in nodes:
    if node0 != node1:
      print("""  staticRouting{0}e->AddHostRouteTo (i{1}{1}e.GetAddress (1), fromLocal, rvector ({{1}},{{1}}));"""
      .format(
        node0,
        node1
      ))

for node0 in nodes:
  for node1 in nodes:
    if node0 != node1:
      print("""  staticRouting{0}e->AddHostRouteTo (i{1}{1}e.GetAddress (1), i{0}{0}e.GetAddress (1), rvector ({{1}},{{1}}));"""
      .format(
        node0,
        node1
      ))
print("")

from format import route_print
route_print()
