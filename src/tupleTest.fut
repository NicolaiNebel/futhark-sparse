import "tupleSparse"
import "MonoidEq"
-- MonoidEq with t=*type* is important to show the compiler the type to be compared with
module intEq : (MonoidEq with t=i32) = {type t = i32
                                        let add = (+)
                                        let mul= (*)
                                        let eq = (i32.==)
                                        let zero= 0i32 }

--module coord = spCoord(intEq)
module coord = spCoord(intEq)

--Make tests
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
  let a = coord.empty 2 3
  let res = coord.toDense a
  let cmp =unflatten 2 3  <| replicate 6 intEq.zero
  in res == cmp

--Make a diagonal matrix
let test4 =
  let len = 5
  let el = 3
  let res = coord.diag len el
  let cmp = replicate len el
  let indChk = reduce (\b1 b2 -> b1 && b2 ) true <| map (\(i,j) -> i==j) res.Inds
  in cmp == res.Vals && indChk

--Update and get tests
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
  in res==intEq.zero

  --Get an element that is in the matrix
let test8 =
  let a = unflatten 2 2 <| iota 4
  let mat = coord.fromDense a
  let res = coord.get mat 0 1
  in res==1

--Transpose tests
--Transpose
let test9 =
  let a = unflatten 2 2 <| iota 4
  let mat = coord.fromDense a
  let res = coord.toDense <| coord.transpose mat
  in reduce (&&) true <| map (\((i,j) : (i32,i32)) -> unsafe(a[i,j]) == unsafe(res[j,i])) <| zip (iota 2) (iota 2)

--Transpose twice
let test10 =
  let a = unflatten 2 2 <| iota 4
  let mat = coord.fromDense a
  let res = coord.toDense <| coord.transpose <| coord.transpose mat
  in res == a

--Transpose diagonal
let test11 =
  let mat = coord.diag 3 1
  let a = coord.toDense mat
  let res = coord.toDense <| coord.transpose mat
  in res == a

--Flatten tests
--flatten


--Map tests
--Map on empty
let test12 =
  let mat = coord.empty 3 3
  let res = coord.sparseMap mat (*3)
  in mat.Vals==res.Vals && mat.Inds==res.Inds

--Map on full values
let test13 =
  let a = unflatten 2 2 <| replicate 4 1
  let mat = coord.fromDense a
  let res = coord.toDense <| coord.sparseMap mat (*2)
  let len = 4 == (length <| flatten res)
  let vals = reduce (&&) true <| map (==2) <| flatten res
  in len && vals

--Map on part
let test14 =
  let a = unflatten 2 2 <| iota 4
  let mat = coord.fromDense a
  let res = coord.sparseMap mat (*2)
  let test = map (*2) <| map (+1) <| iota 3
  in test==res.Vals

--elementwise tests
--elementwise plus with empty
let test15 =
  let a = coord.empty 3 2
  let b = unflatten 3 2 <| iota 6
  let mat = coord.fromDense b
  let res = coord.elementwise a mat (+) 0
  in (length res.Vals == 5) && res.Vals == (map (+1) <| iota 5)

--elementwise times with empty (should be empty)
let test16 =
  let a = coord.empty 3 2
  let b = unflatten 3 2 <| iota 6
  let mat = coord.fromDense b
  let res = coord.elementwise a mat (*) 1
  in res.Vals==mat.Vals && ((coord.getDims res) == (3,2))

-- elementwise with two full
let test17 =
  let a = unflatten 3 2 <| iota 6
  let mat = coord.fromDense a
  let res = coord.elementwise mat mat (*) 1
  let vals = res.Vals
  in reduce (&&) true <| map (\i -> (i+1)**2==unsafe(vals[i])) <| iota 5

--elementwise with all mismatched values
let test18 =
  let a = unflatten 2 2 <| map (\i -> if i%2==0 then 0 else 1) <| iota 4
  let b = unflatten 2 2 <| map (\i -> if i%2==1 then 0 else 1) <| iota 4
  let mata = coord.fromDense a
  let matb = coord.fromDense b
  let res = coord.elementwise mata matb (*) 1
  in (length res.Vals==4) && (reduce (&&) true <| map (==1) res.Vals)

--Mult testsmultiplication with an empty matrix
let test19 =
  let mat = coord.fromDense <| unflatten 3 2 <| iota 6
  let b = coord.empty 2 3
  let res = coord.mul mat b
  in b.Vals==res.Vals && ((coord.getDims res) == (3,3))

--multiplication with two full
let test20 =
  let mat = coord.fromDense <| unflatten 2 2 <| map (+1) <| iota 4
  let res = coord.mul mat mat
  let len = (2,2) == (coord.getDims res)
  let vals = [7,10,15,22] == res.Vals
  in len && vals

--multiplication with all mismatched values
let test21 =
  let a = coord.fromDense [[0,2],[0,2],[0,2]]
  let b = coord.fromDense [[3,3,3],[0,0,0]]
  let res = coord.mul a b
  in (res.Vals==[] ) && ((coord.getDims res) == (3,3))


let main =
  let make = test0 && test1 && test2 && test3 && test4
  let up = test5 && test6 && test7 && test8
  let trans = test9 && test10 && test11
  let list = true
  let maps = test12 && test13 && test14
  let elem = test15 && test16 && test17 && test18
  let mult = test19 && test20 --&& test21
  in make && up && trans && maps&& elem && mult && list