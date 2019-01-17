import "tupleSparse"

let test0 =
  let a = [[1,2],[3,4]]
  let res = coord.fromDense 0 (==) a
  in res.Inds == [(0,0),(0,1),(1,0),(1,1)] && res.Vals == [1,2,3,4]

let test1 =
  let a = unflatten 2 2 <| iota 4
  let res = coord.fromDense 0 (==) a
  in res.Inds == [(0,1),(1,0),(1,1)] && res.Vals == [1,2,3]

let test2 =
  let a = unflatten 2 2 <| iota 4
  let spar = coord.fromDense 0 (==) a
  let dense = coord.toDense 0 spar
  in a==dense

let test3 =
  let a = coord.empty (2,3)
  let ne = 0
  let res = coord.toDense ne a
  let cmp =unflatten 2 3  <| replicate 4 ne
  in res == cmp

let test4 =
  let len = 5
  let el = 3
  let res = coord.diag len el
  let cmp = replicate len el
  let indChk = reduce (\b1 b2 -> b1 && b2 ) true <| map (\(i,j) -> i==j) res.Inds
  in cmp == res.Vals && indChk

let test5 =
  let a = unflatten 2 2 <| iota 4
  let mat = coord.fromDense 0 (==) a
  let res = coord.update mat 1 0 4
  in res.Vals==[1,4,3] && res.Inds==[(0,1),(1,0),(1let a = unflatten 2 2 <| iota 4
  let mat = coord.fromDense 0 (==) a
  let res = coord.update mat 1 0 4
  in res.Vals==[1,4,3] && res.Inds==[(0,1),(1,0),(1,1)],1)]

let test6 =
  let a = unflatten 2 2 <| iota 4
  let mat = coord.fromDense 0 (==) a
  let res = coord.update mat 0 0 4
  in res.Vals==[1,2,3,4] && res.Inds==[(0,1),(1,0),(1,1),(0,0)]

let main =
  let _ = test0 && test1 && test2 && test3 && test4 && test5
  in test6