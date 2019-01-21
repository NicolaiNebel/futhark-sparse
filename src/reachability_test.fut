import "reachability"
import "tupleSparse"

-- wrong dimensions
let test0 =
  let a = [[true,true],[true,true],[true,true]]
  let m = coo_boolean.fromDense a
  in floyd_warshall m == coo_boolean.empty 3 2

-- empty graph
let test1 =
  let m = coo_boolean.fromDense []
  in floyd_warshall m == coo_boolean.empty 0 0

-- completely disjoint graph
let test2 x =
  let a = [ [ false, true, false ]
          , [ true, false, false ]
          , [ false, false, true ]
          ]
  let m = { Inds = [(0,1),(1,0),(2,2)], Vals = [true, true, true], Dims = (3,3) }
  -- let m = coo_boolean.fromDense a
  let res = coo_boolean.toDense <| floyd_warshall m
  in res
  --in res == [ [ true, true, false ]
  --          , [ true, true, false ]
  --          , [ false, false, true]
  --          ]

-- 'real' graph

-- two components
