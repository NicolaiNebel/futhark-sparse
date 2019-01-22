import "lib/github.com/diku-dk/sorts/merge_sort"
import "lib/github.com/diku-dk/segmented/segmented"
import "lib/github.com/diku-dk/sorts/quick_sort"

import "MonoidEq"

module csr (M : MonoidEq) = {
  type elem = M.t
  type csr_matrix = { dims: (i32, i32), vals: []elem, row_ptr: []i32, cols: []i32 }

  type csc_matrix = { dims: (i32, i32), vals: []elem, col_ptr: []i32, rows: []i32 }

  let csrToCsc (m : csr_matrix): csc_matrix =
    -- Grab the dimensions
    let (N,M) = m.dims

    -- Find the lengths of all the rows in m
    let row_lens = map2 (-) (tail m.row_ptr ++ [length m.vals]) m.row_ptr

    -- For each row, expand to (length row) of row index to zip with vals. sum (row_lens) == length m.vals
    let rows: []i32 = expand (.1) (\x _ -> x.2) <| zip row_lens <| iota (length row_lens)
    let tagged_vals: [](elem,i32) = zip m.vals rows

    -- sort the value row pairs by column
    let (sorted_by_col, cols) = zip tagged_vals m.cols |> qsort_by_key (.2) (<=) |> unzip
  
    -- I feel as certain as I can be that this can (and should) be done much, much smarter
    let flags = map2 (!=) cols <| rotate (-1) cols
    let real_cols = zip cols flags |> filter (.2) |> map (.1)
    let col_lens: []i32 = segmented_reduce (+) 0 flags <| replicate (length flags) 1
    let all_col_lens: []i32 = scatter (replicate M 0) real_cols col_lens
    let col_ptr: []i32 = scan (+) 0 <| [0] ++ init all_col_lens

    let (vals, rows) = unzip sorted_by_col

    in { dims = (N,M), vals = vals, col_ptr = col_ptr, rows = rows }

  let cscToCsr (m : csc_matrix): csr_matrix =
    -- Grab the dimensions
    let (N,M) = m.dims

    -- Find the lengths of all the cols in m
    let col_lens = map2 (-) (tail m.col_ptr ++ [length m.vals]) m.col_ptr

    -- For each col, expand to (length col) of col index to zip with vals. sum (col_lens) == length m.vals
    let cols: []i32 = expand (.1) (\x _ -> x.2) <| zip col_lens <| iota (length col_lens)
    let tagged_vals: [](elem,i32) = zip m.vals cols

    -- sort the value col pairs by rowumn
    let (sorted_by_row, rows) = zip tagged_vals m.rows |> qsort_by_key (.2) (<=) |> unzip
  
    -- I feel as certain as I can be that this can (and should) be done much, much smarter
    let flags = map2 (!=) rows <| rotate (-1) rows
    let real_rows = zip rows flags |> filter (.2) |> map (.1)
    let row_lens: []i32 = segmented_reduce (+) 0 flags <| replicate (length flags) 1
    let all_row_lens: []i32 = scatter (replicate M 0) real_rows row_lens
    let row_ptr: []i32 = scan (+) 0 <| [0] ++ init all_row_lens

    let (vals, cols) = unzip sorted_by_row

    in { dims = (N,M), vals = vals, row_ptr = row_ptr, cols = cols }

-- assume row-major
-- Given an element e and a list of elements,
-- Find the first index where e occurs, or -1 if it is not an element
let find_idx_first [n] (e:i32)  (xs:[n]i32) : i32 =
    let es = map2 (\x i -> if e == x then i else n) xs (iota n)
    let res = reduce i32.min n es
    in if res == n then -1 else res

let fromList (dims : (i32, i32)) (xs : []((i32,i32),elem)): csr_matrix =
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

let fromDense [n][m] (matrix: [n][m]elem): csr_matrix =
  let row_idxs = replicated_iota (replicate n m)
  let col_idxs = replicate n (iota m) |> flatten
  let idxs = zip row_idxs col_idxs
  let idxs_mat = zip idxs <| flatten matrix
  let entries = filter (\(_,x) -> !(M.eq M.zero x)) idxs_mat
  in fromList (n,m) entries

let empty (dim : (i32, i32)) : csr_matrix =
  {row_ptr = [], vals=[], dims=dim, cols = []}

let toDense (mat : csr_matrix) : [][]M.t =
    let (N,M) = mat.dims

    in if length mat.vals == 0
    then replicate N (replicate M M.zero)
    else
      let sizes = map2 (-) (tail mat.row_ptr ++ [length mat.vals]) mat.row_ptr
      let rows = replicated_iota sizes
      let inds = map2 (\r c -> r*M + c) rows mat.cols

      let res = scatter (replicate (N*M) M.zero) inds mat.vals
      in unflatten N M res

-- Indexing into CSR matrices
let get (mat : csr_matrix) i j : M.t=
  let row_ptr_with_end = mat.row_ptr ++ [length mat.vals]
  in if i>=mat.dims.1 && j>=mat.dims.2 || i<0 || j<0
  then M.zero -- Should indicate error
  else let part = unsafe(mat.cols[row_ptr_with_end[i]:row_ptr_with_end[i+1]])
       let ind = find_idx_first j part
       in if ind == (-1)
          then M.zero
          else unsafe(mat.vals[mat.row_ptr[i]+ind])

-- Updating a single element of a CSR matrix
-- let update (mat : csr_matrix) i j (el:M.t) : csr_matrix=
--   if i>=mat.dims.1 && j>=mat.dims.2 || i<0 || j<0
--   then mat
--   else let part = unsafe(mat.cols[mat.row_ptr[i]:mat.row_ptr[i+1]])
--        let ind = find_idx_first j part
--        in if ind == (-1)
--           then let len = length mat.vals
--                let ind = unsafe(mat.cols[i+1])-1
--                let vals = scatter (replicate (len+1) M.zero) ((map (\i -> if i>= ind then i+1 else i) (iota len)) ++ [ind] ) (mat.vals++[el])
--                let cols = scatter (replicate (len+1) 0) ((map (\i -> if i>= ind then i+1 else i) (iota len))++[ind]) mat.cols++[ind]
--                in {vals = vals, row_ptr = update (copy mat.row_ptr) i (unsafe(mat.row_ptr[i])+1),cols= cols, dims=mat.dims}
--           else {vals = update (copy mat.vals) (unsafe(mat.cols[i])+ind) el, row_ptr = mat.row_ptr, cols=mat.cols, dims=mat.dims}

let update (mat: csr_matrix) (i: i32) (j: i32) (e: elem): csr_matrix =
  if i>=mat.dims.1 && j>=mat.dims.2 || i<0 || j<0
    then mat
    else
      -- Compute where in mat.vals and mat.cols row i appears
      let (row_start, row_end) = if i == mat.dims.1 - 1 then unsafe((mat.row_ptr[i], length mat.vals))
                                                        else unsafe((mat.row_ptr[i], mat.row_ptr[i+1]))
      let part = unsafe(mat.cols[row_start:row_end])
      let ind = find_idx_first j part
      in if ind != (-1) -- Is element present in the array?
      then { vals = update (copy mat.vals) (unsafe(mat.cols[row_start + ind] + row_start)) e
                 , row_ptr = mat.row_ptr
                 , cols = mat.cols
                 , dims = mat.dims }
      else -- Otherwise we have to make room for it
        -- Put the value at the start of the row. We don't assume cols are sorted
        let (val_fst, val_lst) = split row_start mat.vals
        let vals' = val_fst ++ [e] ++ val_lst
        
        let (col_fst, col_lst) = split row_start mat.cols
        let cols' = col_fst ++ [j] ++ col_lst

        let (ptr_fst, ptr_last) = split (i+1) mat.row_ptr
        let row_ptr' = ptr_fst ++ map (\x -> x+1) ptr_last
        in { vals = vals'
           , cols = cols'
           , row_ptr = row_ptr'
           , dims = mat.dims }


-- ???
let toList (mat : csr_matrix) : []M.t =
    let dim = mat.dims
    let zeros = replicate (dim.1 * dim.2) M.zero
    let new_ptr = mat.row_ptr ++ [length(mat.vals)+1]

    let inds = map(\x -> mat.cols[x] + (find_idx_first x  new_ptr) * dim.2) (iota dim.2) -- fix
    in scatter zeros inds mat.vals

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
let mult_mat_vec (mat: csr_matrix) (vec: []elem) : []elem =
  if mat.dims.2 != length(vec) 
  then []
  else
    let flags = scatter (replicate (length mat.vals) false)
                        mat.row_ptr
                        (replicate mat.dims.1 true)
    let f = \v c -> M.mul v (unsafe(vec[c]))
    in segmented_reduce M.add M.zero flags <| map2 f mat.vals mat.cols

let diag (size : i32) (i : M.t) : csr_matrix =
  { dims    = (size, size)
  , vals    = replicate size i
  , cols    = iota size
  , row_ptr = iota size }

let mul (mat0 : csr_matrix) (mat1 : csc_matrix) : csr_matrix =
    -- Get the dimensions
    -- Should really check that K == K'
    let (M,K) = mat0.dims
    let (K',N) = mat1.dims

    in if (K != K')
    then empty (0,0)
    else
        -- Compute all the intervals of rows in the (val,col) array
        -- Compute which rows are actually present in mat0
        let row_ptr' = tail mat0.row_ptr ++ [ length mat0.vals ]
        let row_lens = map2 (-) row_ptr' mat0.row_ptr
        let (row_lens,real_rows) = zip row_lens (iota (length row_lens)) |> filter (\x -> x.1 != 0) |> unzip

        let from_to = map2 (\x y -> unsafe((mat0.row_ptr[x],mat0.row_ptr[x]+y))) real_rows row_lens

        -- For each row in mat0
        -- Compute the corresponding row in the result
        in loop C = empty (M,N) for i < (length real_rows) do
          let i = unsafe( real_rows[i] )
          -- A_i is the corresponding row in mat0
          let (from, to) = unsafe from_to[i]
          let mat0_vals_cols = zip mat0.vals mat0.cols 
          let A_i = map (\i -> unsafe(mat0_vals_cols[i])) <| range from to 1

          let col_ptr' = tail mat1.col_ptr ++ [ length mat1.vals ]
          let col_lens = map2 (-) col_ptr' mat1.col_ptr  
          let (_,real_cols) = zip col_lens (iota (length col_lens)) |> filter (\x -> x.1 != 0) |> unzip 

          -- Compute the row as a segmented array

          let flags = scatter (replicate (length mat1.rows) false)
                              mat1.col_ptr (replicate mat1.dims.2 true)

          let xs = zip mat1.vals mat1.rows

          let f = \(v,r) ->
            let idx = find_idx_first r (map (.2) A_i)
            in if idx < 0 then M.zero else M.mul A_i[idx].1 v

          let xs' = map f xs

          -- this is supposed to be
          -- for each column (this is from the flags array)
          -- multiply it with the row (reduce_from_row)
          -- and get an array of values of length (length mat1.col_ptr)
          -- that contains the values of each real col multiplied by A_i
          -- So these results map to the points     
          let new_row = segmented_reduce M.add M.zero flags xs' 
          in { dims = C.dims
             , row_ptr = C.row_ptr ++ [ length C.vals ]
             , vals = C.vals ++ new_row
             , cols = C.cols ++ real_cols }
}
