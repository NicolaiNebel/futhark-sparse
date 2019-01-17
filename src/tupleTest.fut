import "tupleSparse"

-- MonoidEq with t=*type* is important to show the compiler the type to be compared with
module intEq : (MonoidEq with t=i32) = {type t = i32
                                        let add = (+)
                                        let mul= (*)
                                        let eq = (i32.==)
                                        let ne= 0i32 }

module coord = spCoord(intEq)

--Make a sparse matrix
let test0 =
  let a = [[1,2],[3,4]]
  let res = coord.fromDense a
  in res.Inds == [(0,0),(0,1),(1,0),(1,1)] && res.Vals == [1,2,3,4]

--Make a sparse matrix that has one neutral element
let test1 =
  let a = unflatten 2 2 <| iota 4
  let res = coord.fromDense a
  in res.Inds == [(0,1),(1,0),(1,1)] && res.Vals == [1,2,3]

--make and redensify a sparse matrix
let test2 =
  let a = unflatten 2 2 <| iota 4
  let spar = coord.fromDense a
  let dense = coord.toDense spar
  in a==dense

--Make an empty sparse matrix, densify and test against the equivalent
let test3 =
  let a = coord.empty (2,3)
  let res = coord.toDense a
  let cmp =unflatten 2 3  <| replicate 6 intEq.ne
  in res == cmp

--Make a diagonal matrix
let test4 =
  let len = 5
  let el = 3
  let res = coord.diag len el
  let cmp = replicate len el
  let indChk = reduce (\b1 b2 -> b1 && b2 ) true <| map (\(i,j) -> i==j) res.Inds
  in cmp == res.Vals && indChk

--Update an already existing element
let test5 =
  let a = unflatten 2 2 <| iota 4
  let mat = coord.fromDense a
  let res = coord.update mat 1 0 4
  in res.Vals==[1,4,3] && res.Inds==[(0,1),(1,0),(1,1)]

--update an element that does not exist in the sparse matrix
let test6 =
  let a = unflatten 2 2 <| iota 4
  let mat = coord.fromDense a
  let res = coord.update mat 0 0 4
  in res.Vals==[1,2,3,4] && res.Inds==[(0,1),(1,0),(1,1),(0,0)]

--Get an element that is not in the matrix - this one is also out of bounds - maybe fail value for this case?
let test7 =
  let a = unflatten 2 2 <| iota 4
  let mat = coord.fromDense a
  let res = coord.get mat 2 3
  in res==intEq.ne

--Get an element that is in the matrix
let test8 =
  let a = unflatten 2 2 <| iota 4
  let mat = coord.fromDense a
  let res = coord.get mat 0 1
  in res==1

--Transpose

--Transpose twice

--flatten

--Map on empty

--Map on full values

--Map on part

--elementwise with an empty matrix

--elementwise with two full

--elementwise with all mismatched values

--multiplication with an empty matrix

--multiplication with two full

--multiplication with all mismatched values


let main =
  let a = test0 && test1 && test2 && test3 && test4 && test5 && test6
  in a && test7 && test8