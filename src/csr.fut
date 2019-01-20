import "lib/github.com/diku-dk/sorts/merge_sort"
import "lib/github.com/diku-dk/segmented/segmented"

import "MonoidEq"

module csr (M : MonoidEq) = {
  type elem = M.t
  type matrix = { dims: (i32, i32), vals: []elem, row_ptr: []i32, cols: []i32 }

-- assume row-major
let find_idx_first [n] (e:i32)  (xs:[n]i32) : i32 =
    let es = map2 (\x i -> if e < x then i else n) xs (iota n)
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

let mul (mat0 : matrix) (mat1 : matrix) : matrix =
    if mat0.dims.2 == mat1.dims.1
    -- 
    then let val_list = toList mat1
	 -- make column vectors
	 let cols = map(\x -> val_list[x*mat1.dims.1:x*mat1.dims.1+mat1.dims.1]) (iota mat1.dims.2)
	 -- multiply every column vector with the matrix
         let vecs = map(\x -> mult_mat_vec mat0 x ) cols
	 in fromDense vecs -- need to transpose. Call csc module!
    else empty (0, 0)



}


-- -- Only updates non-zero values of map
-- let matrix_map 'b (f: a -> b) (m: matrix a): matrix b =
--   { dims = m.dims, vals = map f m.vals, row_ptr = m.row_ptr, cols = m.cols }
