import random

random.seed(4242)

xs = raw_input().split()

size = int(xs[0])
sparsity = float(xs[1])

pairs = int((size ** 2) * sparsity)

lines = [ str(pairs) ]
for i in xrange(pairs):
    x = random.randint(1,size-2)
    y = random.randint(1,size-2)
    lines.append(" ".join([str(x),str(y)]))

    if x > 1600:
        print "ARAGGHHHH"

filename = str(size) + "_" + str(sparsity)
f = open(filename,'w')
f.write("\n".join(lines))
f.close()
