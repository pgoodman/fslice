import collections

class Base(object):
  def __init__(self):
    self.size = 1
    self.byte_sources = {}

  def __getitem__(self, byte):
    self.size = max(self.size, byte + 1)
    return Select(self, byte)

  def __setitem__(self, byte, val):
    self.byte_sources[byte] = val

  def Label(self):
    return "n{}".format(id(self))

  def PrintByteSources(self, next, edges):
    for i, s in self.byte_sources.items():
      next.add(s)
      edges.add("{}:b{} -> {};".format(self.Label(), i, s.Label()))

  def _bytes(self):
    return "|".join("<b{}>".format(i) for i in range(self.size))


class Select(Base):
  def __init__(self, parent, byte):
    Base.__init__(self)
    self.parent = parent
    self.byte = byte

  def Print(self, next, edges):
    next.add(self.parent)

  def Label(self):
    return "{}:b{}".format(self.parent.Label(), self.byte)


class V(Base):
  def __init__(self, val):
    Base.__init__(self)
    self.val = val

  def Print(self, next, edges):
    print "{} [label=\"{{{{{}}}|{}}}\"];".format(self.Label(), self._bytes(), hex(self.val))


class A(Base):
  def __init__(self, op, a, b):
    Base.__init__(self)
    self.op = op
    self.left = a
    self.right = b

  def Print(self, next, edges):
    if isinstance(self.left, V):
      LL = str(self.left.val)
    else:
      next.add(self.left)
      LL = "<left>"
      edges.add("{}:left -> {};".format(self.Label(), self.left.Label()))

    if isinstance(self.right, V):
      RL = str(self.right.val)
    else:
      next.add(self.right)
      RL = "<right>"
      edges.add("{}:right -> {};".format(self.Label(), self.left.Label()))

    print "{} [color=blue label=\"{{ {{ {} }} |{}| {{{}|{}}} }}\"];".format(
        self.Label(), self._bytes(), self.op, LL, RL)


class O(Base):
  def __init__(self, *bytes):
    Base.__init__(self)
    self.bytes = bytes
    self.size = len(self.bytes)

  def __getitem__(self, byte):
    self.size = max(self.size, byte + 1)
    if byte < len(self.bytes):
      return self.bytes[byte]
    else:
      return Select(self, byte)

  def Print(self, next, edges):
    print "{} [label=\"{{{{}}|{{{}}}}}\"];".format(self.Label(), self._bytes())
    for i, b in enumerate(self.bytes):
      next.add(b)
      edges.add("{}:b{} -> {};".format(self.Label(), i, b.Label()))


class B(Base):
  BLOCKS = []

  def __init__(self, size, nr, block_size, block_nr):
    Base.__init__(self)
    self.size = size
    self.nr = nr
    self.block_size = block_size
    self.block_nr = block_nr
    self.BLOCKS.append(self)

  def Print(self, next, edges):
    print "{} [rank=max fillcolor=grey style=filled label=\"{{{{{}}}|{{<size>size = {}|<nr>nr = {} }}}}\"];".format(
        self.Label(), self._bytes(), self.size, self.nr)
    
    if not isinstance(self.block_size, V):
      next.add(self.block_size)
      edges.add("{}:size -> {};".format(self.Label(), self.block_size.Label()))
    
    if not isinstance(self.block_nr, V):
      next.add(self.block_nr)
      edges.add("{}:nr -> {};".format(self.Label(), self.block_nr.Label()))


class N(Base):
  def __init__(self, size):
    Base.__init__(self)
    self.size = size

  def Print(self, next, edges):
    print "{} [fillcolor=green style=filled label=\"{}\"];".format(self.Label(), self._bytes())


class D(Base):
  def __init__(self, size):
    Base.__init__(self)
    self.size = size

  def Print(self, next, edges):
    print "{} [fillcolor=orange style=filled label=\"{}\"];".format(self.Label(), self._bytes())


class M(Base):
  def __init__(self, size, *size_deps):
    Base.__init__(self)
    self.size = size
    self.size_deps = size_deps

  def Print(self, next, edges):
    print "{} [fillcolor=blue style=filled label=\"{}\"];".format(self.Label(), self._bytes())
    #for dep in self.size_deps:
    #  next.add(dep)
    #  edges.add("{}:size -> {}")


def PrintBlocks():
  print "digraph {"
  print "node [shape=record];"
  seen, nodes, edges = set(), set(), set()
    
  blocks = collections.defaultdict(list)
  for b in B.BLOCKS:
    blocks[b.nr].append(b)

  for nr, bs in blocks.items():
    if 1 < len(bs):
      print "subgraph cluster{} {{".format(nr)
      print "rankdir=TB;"
    for b in bs:
      seen.add(b)
      b.Print(nodes, edges)
      b.PrintByteSources(nodes, edges)
    if 1 < len(bs):
      print "}"

  while nodes:
    node = nodes.pop()
    if node not in seen:
      seen.add(node)
      node.Print(nodes, edges)
      node.PrintByteSources(nodes, edges)
  
  for edge in edges:
     print edge

  print "}"

t0 = V(0x0)
