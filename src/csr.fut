import "lib/github.com/diku-dk/sorts/merge_sort"
import "lib/github.com/diku-dk/segmented/segmented"

import "MonoidEq"

module csr (M : MonoidEq) = {
  type elem = M.t
  type matrix = { dims: (i32, i32), vals: []elem, row_ptr: []i32, cols: []i32 }

-- assume row-major
let find_idx_first [n] (e:i32)  (xs:[n]i32) : i32 =
    let es = map2 (\x i -> if e == x then i else n) xs (iota n)
    let res = reduce i32.min n es
    in if res == n then -1 else (res-1)

let fromList (dims : (i32, i32)) (xs : []((i32,i32),elem)): matrix =
  let xs = filter (\(_,x) -> !(M.eq x M.zero)) xs
  let sorted_xs = merge_sort (\((x1,y1),_) ((x2,y2),_) -> if x1 == x2 then y1 <= y2 else x1 < x2) xs
  let (idxs, vals) = unzip sorted_xs
  let (rows, cols) = unzip idxs

  let flags = map2 (!=) rows (rotate (-1) rows)

  let real_rows = zip rows flags |> filter (.2) |> map (.1)
  let row_lens: []i32 = segmented_reduce (+) 0 flags <| replicate (length flags) 1

  let all_row_lens: []i32 = scatter (replicate (dims.1) 0) real_rows row_lens
  let row_ptr: []i32 = scan (\a b -> a + b) 0 <| [0] ++ init all_row_lens

  in { dims = dims, vals = vals, row_ptr = row_ptr, cols = cols }

let fromDense [n][m] (matrix: [n][m]elem): matrix =
  let idxs = replicate m (iota n) |> flatten |> zip (iota m)
  let idxs_mat = zip idxs <| flatten matrix
  let entries = filter (\(_,x) -> !(M.eq M.zero x)) idxs_mat
  in fromList (n,m) entries

let empty (dim : (i32, i32)) : matrix = {row_ptr = [], vals=[], dims=dim, cols = []}

let toDense (mat : matrix) : [][]M.t =
    let dim = mat.dims
    let zeros = replicate (dim.1 * dim.2) M.zero
    let new_ptr = mat.row_ptr ++ [length(mat.vals)+1]

    let inds = map(\x -> mat.cols[x] * find_idx_first x new_ptr) (iota dim.2)
    let res = scatter zeros inds mat.vals
    in unflatten dim.1 dim.2 res

let get (mat : matrix) i j : M.t=
  if i>=mat.dims.1 && j>=mat.dims.2 || i<0 || j<0
  then M.zero
  else let part = unsafe(mat.cols[mat.row_ptr[i]:mat.row_ptr[i+1]])
       let ind = find_idx_first j part
       in if ind == (-1)
          then M.zero
          else unsafe(mat.vals[mat.row_ptr[i]+ind])

let update (mat : matrix) i j (el:M.t) : matrix=
  if i>=mat.dims.1 && j>=mat.dims.2 || i<0 || j<0
  then mat
  else let part = unsafe(mat.cols[mat.row_ptr[i]:mat.row_ptr[i+1]])
       let ind = find_idx_first j part
       in if ind == (-1)
          then let len = length mat.vals
               let ind = unsafe(mat.cols[i+1])-1
               let vals = scatter (replicate (len+1) M.zero) ((map (\i -> if i>= ind then i+1 else i) (iota len)) ++ [ind] ) (mat.vals++[el])
               let cols =scatter (replicate (len+1) 0) ((map (\i -> if i>= ind then i+1 else i) (iota len))++[ind]) mat.cols++[ind]
               in {vals = vals, row_ptr = update (copy mat.row_ptr) i (unsafe(mat.row_ptr[i])+1),cols= cols, dims=mat.dims}
          else {vals = update (copy mat.vals) (unsafe(mat.cols[i])+ind) el, row_ptr = mat.row_ptr, cols=mat.cols, dims=mat.dims}

let toList (mat : matrix) : []M.t =
    let dim = mat.dims
    let zeros = replicate (dim.1 * dim.2) M.zero
    let new_ptr = mat.row_ptr ++ [length(mat.vals)+1]

    let inds = map(\x -> mat.cols[x] + (find_idx_first x  new_ptr) * dim.2) (iota dim.2) -- fix
    in scatter zeros inds mat.vals

-- Doesnt compile du to vals: *[]M.t instead of vals: []M.t
--let scale (mat : matrix) (i : M.t) : matrix =
--  let tmp = copy mat.vals
--  let newval = map(\x -> M.mul x i) tmp
--    in {dims = mat.dims, vals = newval, row_prt = mat.row_ptr, cols = mat.cols}
let segmented_replicate [n] (reps:[n]i32) (vs:[n]i32) : []i32 =
  let idxs = replicated_iota reps
  in map (\i -> unsafe vs[i]) idxs

let idxs_to_flags [n] (is : [n]i32) : []bool =
    let vs = segmented_replicate is (iota n)
    in map2 (!=) vs ([0] ++ vs[:length vs-1])


  -- in order to extend this to work on matrix X matrix multiplications
  -- We can do one column vector at the time
  -- We could transform the second matrix in csc format so that the rows and the colums match up
  -- if we do implement a csr -> csc method then we can do transpose in O(1) time by just
  -- changing the type and swapping the dimensions from.
  -- tranpose matrix CSR -> matrix csc ... also works the other way
let mult_mat_vec (mat : matrix) (vec : []M.t) : []M.t  =
    if mat.dims.2 == length(vec) 
    then let multis = map2 (\x y -> M.mul x vec[y]) mat.vals mat.cols
	 let lens = length(mat.vals)
	 let testflags = idxs_to_flags mat.row_ptr

	 -- same as above?
         let tmp =  scatter (replicate (reduce (+) 0 [lens]) 0) ([0] ++ mat.row_ptr) (iota lens)
	 let flags = map (>0) tmp
	 -- could maybe remove that part

         let segs = segmented_scan (M.add) M.zero flags multis
	 let flag_1 = flags ++ [true]
	 let newf = map(\x -> flag_1[x+1]) (iota (length(flags)))

	 
	 let (_,res) = unzip (filter (.1) <| zip newf segs)
         in res
    else [M.zero] -- case the dims does not match


--let diag (size : i32) (i : M.t) : matrix =
--  let (inds, vals) = 

module dual = csc(M)

let mul (mat0 : matrix) (mat1 : matrix) : matrix =
    -- Get the dimensions
    -- Should really check that K == K'
    let (M,K) = mat0.dims
    let (K',N) = mat1.dims

    -- Get mat1 on column form
    let mat1 = dual.fromCsr(mat1)

    -- Compute all the intervals of rows in the (val,col) array
    -- Compute which rows are actually present in mat0
    let row_ptr' = tail mat0.row_ptr ++ [ length mat0.vals ]
    let row_lens = map2 (-) row_ptr' mat0.row_ptr
    let (row_lens,real_rows) = zip row_lens (iota (length row_lens)) |> filter (\x -> x.1 != 0) |> unzip

    let from_to = map2 (\x y -> (x,x+y)) real_rows row_lens

    -- For each row in mat0
    -- Compute the corresponding row in the result
    in loop C = empty (M,N) for i < (length real_rows) do
      let i = unsafe( real_rows[i] )
      -- A_i is the corresponding row in mat0
      let (from, to) = unsafe from_to[i]
      let mat0_vals_cols = zip mat0.vals mat0.cols
      let A_i = map (\i -> unsafe(mat0_vals_cols[i])) <| range from to

      let col_ptr' = tail mat1.col_ptr ++ [ length mat1.vals ]
      let col_lens = map2 (-) col_ptr' mat1.col_ptr
      let (col_lens,real_cols) = zip col_lens (iota (length col_lens)) |> filter (\x -> x.1 != 0) |> unzip

      -- Compute the row as a segmented array
      -- No need to process col_ptr, the cols we just filtered out are duplicate values in it anyways
      let flags = idxs_to_flags mat1.col_ptr
      let xs = zip mat1.vals mat1.rows

      let f = \(v,r) ->
        let idx = find_idx_first r (map (.2) A_i)
        in if idx < 0 then 0 else A_i[idx].1 * v

      let xs' = map f xs

      -- this is supposed to be
      -- for each column (this is from the flags array)
      -- multiply it with the row (reduce_from_row)
      -- and get an array of values of length (length mat1.col_ptr)
      -- that contains the values of each real col multiplied by A_i
      -- So these results map to the points
      let new_row = segmented_reduce (+) 0 flags xs'
      let new_row = zip new_row real_cols

      in foldl (\C (v, c) -> update C i c v) C new_row
}

