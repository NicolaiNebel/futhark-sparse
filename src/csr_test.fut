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
-- entry: mulTest
-- input { [1,0] [0,1] }
-- output { [1,0] [0,1] }

entry mulTest (row1: []i32) (row2: []i32): ([]i32, []i32) =
  let m1 = csr_i32.fromDense [ row1, row2 ]
  --let m2 = csr_i32.fromCsr(csr_i32.fromDense(m2))
  --let res = csr_i32.mul m1 m2
  let xs = csr_i32.toDense m1
  in unsafe( (xs[0],xs[1]) )
