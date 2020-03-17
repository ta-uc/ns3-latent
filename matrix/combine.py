with open("created","r") as f:
  created = f.read()

with open("template.cc", "r") as f:
  template = f.read()

createdcc = template.replace("///INSERT///",created)

with open("../scratch/created.cc","w") as f:
  print(createdcc,file=f)

