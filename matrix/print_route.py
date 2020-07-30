from topology import nodes, links, links_all


def print_route(route_lists, capas):
  print(
  f"""  NodeContainer c, c_e;
  c.Create ({len(nodes)});
  c_e.Create ({len(nodes)});

  InternetStackHelper internet;
  internet.Install (c);
  internet.Install (c_e); """
  )

  #point-to-point
  for link in links:
    print(
      f"""  NodeContainer n{link} = NodeContainer (c.Get ({nodes.index(link[0])}), c.Get ({nodes.index(link[1])}));"""
    )

  print("")

  for node in nodes:
    print(
      f"""  NodeContainer n{node}{node}e = NodeContainer (c.Get ({nodes.index(node)}), c_e.Get ({nodes.index(node)}));"""
    )
  print("")

  print(
  """  PointToPointHelper p2p, p2p_l;
  p2p.SetChannelAttribute ("Delay", StringValue ("1ms"));
  p2p_l.SetDeviceAttribute ("DataRate", StringValue ("300Mbps"));
  """
  )

  for link in links:
    print(
      f"""  NetDeviceContainer d{link} = p2p.Install (n{link});"""
    )

  print("")

  for node in nodes:
    print(
      f"""  NetDeviceContainer d{node}{node}e = p2p_l.Install (n{node}{node}e);"""
    )

  print("")

  for node_num in range(len(nodes)):
    part_links = [link for link in links_all if link[0] == nodes[node_num]]
    for part_link_num in range(len(part_links)):
      print(
        f"""  Config::Set("/NodeList/{node_num}/$ns3::Node/DeviceList/{part_link_num + 1}/$ns3::PointToPointNetDevice/DataRate", DataRateValue (DataRate("{capas[part_links[part_link_num]]}Mbps")));"""
      )

  print("")

  print(
  """  TrafficControlHelper tch;
  tch.SetRootQueueDisc ("ns3::FqCoDelQueueDisc");"""
  )

  for link in links:
    print(
      f"""  tch.Install (d{link});"""
    )

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
      print(
        f"""  d{d}.Get ({n})->TraceConnectWithoutContext("PhyTxEnd", MakeBoundCallback(&linkPktCount, {k}));"""
      )
      k += 1

  print("")

  l = 0
  for node_num in range(len(nodes)):
    part_links = [link for link in links_all if link[0] == nodes[node_num]]
    for part_link_num in range(len(part_links)):
      print(
        f"""  Config::ConnectWithoutContext ("/NodeList/{node_num}/$ns3::TrafficControlLayer/RootQueueDiscList/{part_link_num + 1}/Drop", MakeBoundCallback (&linkPktLossCount, {l}));"""
      )
      l += 1

  print("")
  print("  Ipv4AddressHelper ipv4;")

  i = 1
  for link in links:
    print(
    f"""  ipv4.SetBase ("10.1.{i}.0", "255.255.255.0");
    Ipv4InterfaceContainer i{link} = ipv4.Assign (d{link});"""
    )
    i += 1

  print("")
  print("  std::vector <ns3::Ipv4Address> sinkAddresses;")

  for node in nodes:
    print(
    f"""  ipv4.SetBase ("192.168.{nodes.index(node)+1}.0", "255.255.255.0");
    Ipv4InterfaceContainer i{node}{node}e = ipv4.Assign (d{node}{node}e);
    sinkAddresses.push_back(i{node}{node}e.GetAddress(1));"""
    )

  print("")

  for node in nodes:
    print(
      f"""  Ptr<Ipv4> ipv4{node} = c.Get ({nodes.index(node)})->GetObject<Ipv4> ();"""
    )

  print("")

  for node in nodes:
    print(
      f"""  Ptr<Ipv4> ipv4{node}e = c_e.Get ({nodes.index(node)})->GetObject<Ipv4> ();"""
    )

  print("")
  print("  Ipv4StaticRoutingHelper ipv4RoutingHelper;")

  for node in nodes:
    print(
      f"""  Ptr<Ipv4StaticRouting> staticRouting{node} = ipv4RoutingHelper.GetStaticRouting (ipv4{node});"""
    )

  print("")

  for node in nodes:
    print(
      f"""  Ptr<Ipv4StaticRouting> staticRouting{node}e = ipv4RoutingHelper.GetStaticRouting (ipv4{node}e);"""
    )

  print("")
  print("""  Ipv4Address fromLocal = Ipv4Address ("102.102.102.102");""")
  print("")

  for node0 in nodes:
    for node1 in nodes:
      if node0 != node1:
        print(
          f"""  staticRouting{node0}e->AddHostRouteTo (i{node1}{node1}e.GetAddress (1), fromLocal, rvector ({{1}},{{1}}));"""
        )

  for node0 in nodes:
    for node1 in nodes:
      if node0 != node1:
        print(
          f"""  staticRouting{node0}e->AddHostRouteTo (i{node1}{node1}e.GetAddress (1), i{node0}{node0}e.GetAddress (1), rvector ({{1}},{{1}}));"""
        )

  print("")



  for dct in route_lists:
    print("")
    first = list(dct.keys())[0][0]
    end = list(dct.keys())[0][1]

    link_list = []
    prob_dict = {}
    for route, prob in zip(dct.keys(), dct.values()):
      prob_dict[route[3:]] = prob
      link_list.append(route[3:])

    links_in_route = list(set(link_list))

    routes = []
    link_tops = [link[0] for link in links_in_route]
    link_tops = list(set(link_tops))

    for link_top in link_tops:
      same_linktop_links =[]
      for link in links_in_route:
        if link[0] == link_top:
          same_linktop_links.append(link)
      routes.append(same_linktop_links)

    #エッジノード
    oif = 1
    for link in links:
      oif += link.count(end)
    print(f"""  staticRouting{end}->AddHostRouteTo (i{end}{end}e.GetAddress(1), i{first}{first}e.GetAddress(1), rvector({{{oif}}},{{1}})); //{end}->{end}e""")

    for route in routes:
      if len(route) == 1: #分岐なし
        link_part_list = [link for link in links if route[0][0] in list(link)]
        try:
          oif = link_part_list.index(route[0][0]+route[0][1]) + 1
        except:
          oif = link_part_list.index(route[0][1]+route[0][0]) + 1
        print(f"""  staticRouting{route[0][0]}->AddHostRouteTo (i{end}{end}e.GetAddress(1), i{first}{first}e.GetAddress(1), rvector({{{oif}}},{{1}})); //{route[0][0]}->{route[0][1]}""")
      if len(route) > 1:
        link_part_list_a = [link for link in links if route[0][0] in list(link)]
        link_part_list_b = [link for link in links if route[1][0] in list(link)]
        try:
          oif_a = link_part_list_a.index(route[0][0]+route[0][1]) + 1
        except:
          oif_a = link_part_list_a.index(route[0][1]+route[0][0]) + 1
        try:
          oif_b = link_part_list_b.index(route[1][0]+route[1][1]) + 1
        except:
          oif_b = link_part_list_b.index(route[1][1]+route[1][0]) + 1
        if sum([prob_dict[i] for i in route]) < 0.99:
          small = min([prob_dict[i] for i in route])
          large = max([prob_dict[i] for i in route])
          a = f"""  staticRouting{route[0][0]}->AddHostRouteTo (i{end}{end}e.GetAddress(1), i{first}{first}e.GetAddress(1), rvector({{{oif_a,oif_b}}},{{{[prob_dict[i] for i in route]}}})); //{route[0][0]}->{route[0][1]},{route[1][1]}"""
          a = a.replace(str(small),str(small/(large+small)))
          a = a.replace(str(large),str(large/(large+small)))
        else:
          a = f"""  staticRouting{route[0][0]}->AddHostRouteTo (i{end}{end}e.GetAddress(1), i{first}{first}e.GetAddress(1), rvector({{{oif_a,oif_b}}},{{{[prob_dict[i] for i in route]}}})); //{route[0][0]}->{route[0][1]},{route[1][1]}"""
        print(a.replace("[","").replace("]","").replace("{(","{").replace(")}","}"))