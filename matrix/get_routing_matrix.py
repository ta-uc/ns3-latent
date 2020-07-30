link_numbering = {
  "AB": 0, "AC": 1, "BA": 2, "BC": 3,
  "BD": 4, "CA": 5, "CB": 6, "CE": 7,
  "DB": 8, "DF": 9, "EC": 10,"EF": 11,
  "EH": 12,"FE":13, "FD": 14,"FI": 15,
  "GH": 16,"GK":17, "HE": 18,"HG": 19,
  "HI": 20,"IF":21, "IH": 22,"IJ": 23,
  "JI": 24,"JK":25, "KG": 26,"KJ": 27
}

def get_routing_matrix(routes):

  routing_matrix = []
  for route in routes:
    matrix_one_row = [0] * len(link_numbering)
    for od_pair in route.keys():
      matrix_one_row[link_numbering[od_pair[3:5]]] = route[od_pair]
    routing_matrix.append(matrix_one_row)
  return routing_matrix
