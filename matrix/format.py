from route import route_lists
from topology import links

def route_print():
  for dct in route_lists:
    print("")
    first = list(dct.keys())[0][0]
    end = list(dct.keys())[0][1]

    link_list = []
    prob_dict = {}
    for route,prob in zip(dct.keys(),dct.values()):
      prob_dict[route[3:]] = prob
      link_list.append(route[3:])

    links = list(set(link_list))

    routes = []
    link_tops = [link[0] for link in links]
    link_tops = list(set(link_tops))

    for link_top in link_tops:
      same_linktop_links =[]
      for link in links:
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