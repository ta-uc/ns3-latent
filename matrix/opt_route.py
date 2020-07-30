import pulp
from topology import nodes, links_all as links, capas as c
from od_data_before import actual as t
import itertools

def opt_route(OrigNode, DestNode):
    # links 全リンク
    # nodes 全ノード
    # c[ij] リンク容量
    # ods[pq] ODペア
    # x[pqij] リンク(ij)のOD(pq)トラフィック割合
    r = pulp.LpVariable(f"最大リンク使用率", 0, 1, pulp.LpContinuous)
    
    # ODペア初期化
    ods = [o+d for o, d in itertools.permutations(nodes, 2)]

    # ODトラヒック割合　初期化　(6.2e)
    x = {pq+ij: pulp.LpVariable(f"{pq}_{ij}", 0, 1, pulp.LpContinuous) for pq, ij in itertools.product(ods, links)}

    problem = pulp.LpProblem('リンク使用率最小化', pulp.LpMinimize)

    # 目的関数
    # (6.2a)
    problem += r

    # 制約条件 #
    # (6.2b) 中継ノード
    for pq, i in itertools.product(ods, nodes):
        if i not in list(pq):
            a = [i+j for j in nodes if i+j in links]
            b = [j+i for j in nodes if j+i in links]
            problem += pulp.lpSum(x[pq+ij] for ij in a) - \
                pulp.lpSum(x[pq+ji] for ji in b) == 0

    for pq in ods:
        # (6.2c)　発ノード
        _ij = [ij for ij in links if ij[0] == pq[0]]
        _ji = [ji for ji in links if ji[1] == pq[0]]
        problem += pulp.lpSum(x[pq+ij] for ij in _ij) - \
            pulp.lpSum(x[pq+ji] for ji in _ji) == 1

    # リンク容量＞リンクトラヒック
    for ij in links:
        problem += pulp.lpSum(t[pq]*x[pq+ij] for pq in ods) <= c[ij] * r

    # 計算実行
    result_status = problem.solve()

    # 結果表示
    # print(pulp.LpStatus[result_status])
    # print(pulp.value(problem.objective))

    with open("util_opt_route.py", "w") as rf:
        print("route_lists = [", file=rf)
        for pq in ods:
            print("{", file=rf)
            for ij in links:
                if x[pq+ij].value() > 0:
                  # if x[pq+ij].value() != 1.0:
                    print(f'"{x[pq+ij]}":{x[pq+ij].value()},', file=rf)
            print("},", file=rf)
        print("]", file=rf)