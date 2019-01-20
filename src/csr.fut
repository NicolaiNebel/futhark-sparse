import "lib/github.com/diku-dk/sorts/merge_sort"
import "lib/github.com/diku-dk/segmented/segmented"

import "MonoidEq"

module csr (M : MonoidEq) = {
  type elem = M.t
  type matrix = { dims: (i32, i32), vals: []elem, row_ptr: []i32, cols: []i32 }

-- assume row-major
let fromList (dims : (i32, i32)) (xs : []((i32,i32),elem)): matrix =
  let xs = filter (\(_,x) -> !(M.eq x M.ne)) xs
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
  let entries = filter (\(_,x) -> !(M.eq M.ne x)) idxs_mat
  in fromList (n,m) entries


  let empty (dim : (i32,i32)) : matrix = {row_ptr = [], vals=[], dims=dim}

let toDense (mat : matrix) =
    let dim = mat.dims
    let zeros = map(\x -> replicate (dim.1 * dims2)) M.ne
    let new_ptr = mat.row_ptr ++ [length(mat.vals)+1]

    let inds = map(\x -> mat.cols[x] + find_idx_first(x, new_ptr)) (iota dim.2)
    let res = scatter zeros inds mat.vals
    in unflatten dim.1 dim.2 res

let toList (mat : matrix) : vector =
    let dim = mat.dims
    let zeros = map(\x -> replicate (dim.1 * dims2)) M.ne
    let new_ptr = mat.row_ptr ++ [length(mat.vals)+1]

    let inds = map(\x -> mat.cols[x] + find_idx_first(x, new_ptr)) (iota dim.2)
    in scatter zeros inds mat.vals


let scale (mat : matrix) (i : i32) : matrix =
    let newval = map(\x -> M.mul x i) mat.vals
    in {dims = mat.dims, vals = newval, row_prt = mat.row_ptr, cols = mat.cols}


let idxs_to_flags [n] (is : [n]i32) : []bool =
    let vs = segmented_replicate is (iota n)
    in map2 (!=) vs ([0] ++ vs[:length vs-1])


    
let find_idx_first 'v [n] (e:v)  (xs:[n]v) : i32 =
    let es = map2 (\x i -> if e < x then i else n) xs (iota n)
    let res = reduce i32.min n es
    in if res == n then -1 else res


  -- in order to extend this to work on matrix X matrix multiplications
  -- We can do one column vector at the time
  -- We could transform the second matrix in csc format so that the rows and the colums match up
  -- if we do implement a csr -> csc method then we can do transpose in O(1) time by just
  -- changing the type and swapping the dimensions from.
  -- tranpose matrix CSR -> matrix csc ... also works the other way
let mult_mat_vec (mat : matrix) (vec : vector) : vec  =
    if mat.dims.2 == vec.dims.1 
    then let multis = map (\(x y) -> x * vec.vals[y]) mat.vals mat.cols
	 let lens = length(mat.vals)
	 let testflags = idxs_to_flags mat.row_ptr

	 -- same as above?
         let tmp =  scatter (replicate (reduce (+) 0 [lens]) 0) ([0] ++ mat.row_ptr) (iota lens)
	 let flags = map (>0) tmp
	 -- could maybe remove that part

         let segs = segmented_scan (+) 0 multis flags
	 let newflags = flags ++ [1]
	 let newf = map(\x -> if newflags[x+1] == 1 then 1 else 0) (iota (length(flags)-1))

	 -- wrong flag vector! must be the last from each segment!
	 let (_,res) = unzip (filter (\(b,_) -> b) <| zip newf  segs)
         in {vals = res}
    else [0] -- case the dims does not match


let mul (mat0 : matrix) (mat1 : matrix) : matrix =
    if mat0.dims.2 == mat1.dims.1
    -- 
    then let val_list = toList mat1
	 -- make column vectors
	 let cols = map(\x -> val_list[x*mat1.dims.1:x*mat1.dims.1+mat1.dims.1]) (iota mat1.dims.2)
	 -- multiply every column vector with the matrix
         let vecs = map(\x -> mult_map_vec(mat0, x)) cols
         in vecs -- need to tranpose
    else empty (0,0)

}


-- -- Only updates non-zero values of map
-- let matrix_map 'b (f: a -> b) (m: matrix a): matrix b =
--   { dims = m.dims, vals = map f m.vals, row_ptr = m.row_ptr, cols = m.cols }
}
