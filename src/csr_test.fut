import "csr"
import "MonoidEq"

module csr_i32 = csr(monoideq_i32)

-- ==
-- entry: fromListTest
-- input { 5 5 [1,1,3] [1,2,3] [2,3,5] }
-- output { 5 5 [2,3,5] [0,0,2,2,3] [1,2,3] }

entry fromListTest (x: i32) (y: i32) (rows: []i32) (cols: []i32) (vals: []i32): (i32, i32, []i32, []i32, []i32) =
  let idxs = zip rows cols
  let xs = zip idxs vals
  let res = csr_i32.fromList (x,y) xs
  in ( res.dims.1
     , res.dims.2
     , res.vals
     , res.row_ptr
     , res.cols
     )

-- ==
-- entry: toDenseFromDenseIdentTest
-- input { [[1i32, 0i32], [0i32, 1i32]] }
-- output { [[1i32, 0i32], [0i32, 1i32]] }
-- input { [[1i32, 0i32, 4i32], [0i32, 1i32, 1i32]] }
-- output { [[1i32, 0i32, 4i32], [0i32, 1i32, 1i32]] }

entry toDenseFromDenseIdentTest (m: [][]i32): [][]i32 =
  csr_i32.toDense <| csr_i32.fromDense m

-- ==
-- entry: csrToCscIdentTest
-- input { [[1i32, 0i32], [0i32, 1i32]] }
-- output { [[1i32, 0i32], [0i32, 1i32]] }
-- input { [[1i32, 0i32, 4i32], [0i32, 1i32, 1i32]] }
-- output { [[1i32, 0i32, 4i32], [0i32, 1i32, 1i32]] }

entry csrToCscIdentTest (m: [][]i32): [][]i32 =
  csr_i32.toDense <| csr_i32.cscToCsr <| csr_i32.csrToCsc <| csr_i32.fromDense m

-- ==
-- entry: getTest
-- input { [[1i32, 0i32], [0i32, 1i32]] 0 0 }
-- output { 1 }
-- input { [[1i32, 0i32], [0i32, 1i32]] 0 1 }
-- output { 0 }
-- input { [[1i32, 0i32], [0i32, 1i32]] 1 0 }
-- output { 0 }
-- input { [[1i32, 0i32], [0i32, 1i32]] 1 1 }
-- output { 1 }
-- input { [[1i32, 0i32, 4i32], [0i32, 1i32, 1i32]] 0 2 }
-- output { 4 }
-- input { [[1i32, 0i32, 1i32], [0i32, 4i32, 1i32]] 1 1 }
-- output { 4 }
-- input { [[1i32, 0i32], [0i32, 1i32], [1i32, 0i32], [0i32, 1i32], [1i32, 4i32], [0i32, 1i32]] 4 1 }
-- output { 4 }

entry getTest (m: [][]i32) (i: i32) (j: i32): i32 =
  let m = csr_i32.fromDense m
  in csr_i32.get m i j

-- ==
-- entry: updateTest
-- input { [[1i32, 0i32], [0i32, 1i32]] 0 0 2}
-- output { [[2i32, 0i32], [0i32, 1i32]] }
-- input { [[1i32, 0i32, 4i32], [0i32, 1i32, 1i32]] 0 1 5 }
-- output { [[1i32, 5i32, 4i32], [0i32, 1i32, 1i32]] }

entry updateTest (m: [][]i32) (i: i32) (j: i32) (x:i32): [][]i32 =
  let m = csr_i32.fromDense m
  in csr_i32.update m i j x |> csr_i32.toDense

-- ==
-- entry: multMatVecTest
-- input { [[1, 0], [0,1]] [2,4] }
-- output { [2,4] }
-- input { [[1, 0], [0,1]] [1,5] }
-- output { [1,5] }
-- input { [[1, 1], [0,1]] [1,5] }
-- output { [6,5] }
-- input { [[2, 1], [0,1]] [1,5] }
-- output { [7,5] }
-- input { [[2, 1], [0,1], [2,2]] [1,5] }
-- output { [7,5,12] }

entry multMatVecTest (m : [][]i32) (v: []i32) : []i32 =
  csr_i32.mult_mat_vec (csr_i32.fromDense m) v

-- ==
-- entry: mulTest
-- input { [[1,2],[3,4]] [[1,2],[3,4]] }
-- output { [[7,10],[15,22]] }
-- input { [[1,2],[3,4]] [[1,2],[3,4]] }
-- output { [[7,10],[15,22]] }
-- input { [[1,0],[3,4]] [[1,2],[3,0]] }
-- output { [[1,2],[15,6]] }
-- input { [[1,2,3],[4,5,6]] [[0,0],[0,0],[0,0]] }
-- output { [[0,0],[0,0]] }
-- input { [[0,0],[0,0],[0,0]] [[1,2,3],[4,5,6]] }
-- output { [[0,0,0],[0,0,0],[0,0,0]] }

entry mulTest (m1: [][]i32) (m2: [][]i32): [][]i32 =
  let m1 = csr_i32.fromDense m1
  let m2 = m2 |> csr_i32.fromDense |> csr_i32.csrToCsc
  let res = csr_i32.mul m1 m2
  in csr_i32.toDense res
