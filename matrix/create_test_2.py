import itertools

# nodes = [
#   "A","B",
#   "C","D",
#   "E","F",
#   "G","H",
#   "I","J",
#   "K"
#   ]

# links = [
#   "AB","BA",
#   "AC","CA",
#   "BC","CB",
#   "BD","DB",
#   "CE","EC",
#   "CD","DC",
#   "EF","FE",
#   "EH","HE",
#   "DF","FD",
#   "FI","IF",
#   "GH","HG",
#   "HI","IH",
#   "GK","KG",
#   "IJ","JI",
#   "JK","KJ"
# ]

nodes = [
  "A","B",
  "C","D",
  "E","F",
  "G","H"
  ]

links = [
  "AB",
  "AC",
  "AD",
  "BE",
  "BF",
  "CE",
  "CF",
  "DE",
  "DF",
  "GA",
  "EH",
  "FH"
]

routes = [
  "GADF",
  "ACFH"
]

# routes = [
#   "ACEFIJK",
#   "ABDF",
#   "GHEF",
#   "KJI",
#   "GKJ",
#   "IJK"
# ]

print("""  NodeContainer c;
  c.Create ({0});

  InternetStackHelper internet;
  internet.Install (c);
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

print(
  """  PointToPointHelper p2p;
  p2p.SetDeviceAttribute ("DataRate", StringValue ("5Mbps"));
  p2p.SetChannelAttribute ("Delay", StringValue ("1ms"));
  """)

for link in links:
  print(
  """  NetDeviceContainer d{0} = p2p.Install (n{0});"""
  .format(link))

print("")

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

for node in nodes:
  print("""  Ptr<Ipv4> ipv4{0} = c.Get ({1})->GetObject<Ipv4> ();"""
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
print("""  Ipv4Address fromLocal = Ipv4Address ("102.102.102.102");""")

# routes = [
#     "ABEFH"
#   ]

for route in routes:
  route_list = list(route)
  if(route[-2:] in links):
    dest = """i{}{}.GetAddress (1,0)""".format(
      route[-2],
      route[-1]
    )
  else:
    dest = """i{}{}.GetAddress (0,0)""".format(
          route[-1],
          route[-2]
        )

  if(route[:2] in links):
    source = """i{}{}.GetAddress (0,0)""".format(
      route[0],
      route[1]
    )
  else:
    source = """i{}{}.GetAddress (1,0)""".format(
          route[1],
          route[0]
        )
  for i in range(len(route_list) - 1):
    link_part_list = [link for link in links if route_list[i] in list(link)]
    try:
      oif = link_part_list.index(route_list[i]+route_list[i+1])
    except:
      oif = link_part_list.index(route_list[i+1]+route_list[i])
    if i == 0:
      print("""  staticRouting{0}->AddHostRouteTo ({1}, fromLocal, rvector ({{{2}}},{{1}}));//{0}->{3}"""
      .format(
        route_list[i],
        dest,
        oif+1,
        route_list[i+1]
      ))
    else:
      print("""  staticRouting{0}->AddHostRouteTo ({1}, {2}, rvector ({{{3}}},{{1}}));//{0}->{4}"""
      .format(
        route_list[i],
        dest,
        source,
        oif+1,
        route_list[i+1]
      ))
  print("")
