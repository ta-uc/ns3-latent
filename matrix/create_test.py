nodes = [
  "A","B",
  "C","D",
  "E","F"
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
]

routes = {
}

#point-to-point
for link in links:
  print("""NodeContainer n{} = NodeContainer (c.Get ({}), c.Get ({}));"""
  .format(
    link,
    nodes.index(link[0]),
    nodes.index(link[1])
  ))

for link in links:
  print("""NetDeviceContainer d{0} = p2p.Install (n{0});"""
  .format(link))

i = 1
for link in links:
  print("""ipv4.SetBase ("10.1.1.{0}", "255.255.255.0");
  Ipv4InterfaceContainer i{1} = ipv4.Assign (d{1});"""
  .format(
    i,
    link
  ))
  i += 1




