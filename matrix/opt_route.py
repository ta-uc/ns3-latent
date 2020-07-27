import pulp
from topology import nodes, links_all as links, capas as c


def opt_route_capa(OrigNode, DestNode, od_rate):
  # links 全リンク
  # nodes 全ノード
  # c[ij] リンク容量
  ods = [] # ods[pq] ODペア
  t = {} # t[pq] ODトラフィック
  w = {} # w[ij] リンク増分
  x = {} # x[pqij] リンク(ij)のOD(pq)トラフィック割合

  if od_rate == "latent":
    from odrate import latent as t
  elif od_rate == "actual":
    from odrate import actual as t
  else:
    # ODトラヒック初期化
    for p in nodes:
      for q in nodes:
        if p != q:
          t[p+q] = 5
    # 増加OD設定 ノードp -> ノードq
    _p = OrigNode
    _q = DestNode
    t[_p+_q] = 35


  # リンク増分初期化
  for ij in links:
    w[ij] = pulp.LpVariable(f"リンク帯域増分_{ij}",0,None,pulp.LpContinuous)

  # ODペア初期化
  for o in nodes:
    for d in nodes:
      if o != d:
        ods.append(o+d)

  # ODトラヒック割合　初期化　(6.2e)
  for pq in ods:
    for ij in links:
      x[pq+ij] = pulp.LpVariable(f"{pq}_{ij}",0,1,pulp.LpContinuous)
      
  problem = pulp.LpProblem('リンク増分ベクトル最小化', pulp.LpMinimize)

  # 目的関数
  # (6.2a)
  problem += pulp.lpSum(w)


  # 制約条件 #
  for pq in ods:
    # (6.2b) 中継ノード
    for i in nodes:
      if i not in list(pq):
        a = [i+j for j in nodes if i+j in links]
        b = [j+i for j in nodes if j+i in links]
        problem += pulp.lpSum(x[pq+ij] for ij in a) - pulp.lpSum(x[pq+ji] for ji in b ) == 0

  for pq in ods:
    # (6.2c)　発ノード
    _ij = [ij for ij in links if ij[0] == pq[0]]
    _ji = [ji for ji in links if ji[1] == pq[0]]
    problem += pulp.lpSum(x[pq+ij] for ij in _ij) - \
              pulp.lpSum(x[pq+ji] for ji in _ji) == 1

  # リンク容量＞リンクトラヒック
  for ij in links:
    problem += pulp.lpSum(t[pq]*x[pq+ij] for pq in ods) <= c[ij] + w[ij]

  # 計算実行
  result_status = problem.solve()

  # 結果表示
  # print(pulp.LpStatus[result_status])
  # print(pulp.value(problem.objective))

  with open("route.py","w") as rf:
    print("route_lists = [",file=rf)
    for pq in ods:
      print("{",file=rf)
      for ij in links:
        if x[pq+ij].value() > 0:
          # if x[pq+ij].value() != 1.0:
            print(f'"{x[pq+ij]}":{x[pq+ij].value()},',file=rf)
      print("},",file=rf)
    print("]",file=rf)
  
  with open("capas_incd.py","w") as capafile:
    print("capas = {",file=capafile)
    for ij in links:
      print(f'"{ij}":{w[ij].value() + int(c[ij])},',file=capafile)
    print("}",file=capafile)