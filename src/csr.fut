import "lib/github.com/diku-dk/sorts/merge_sort"
import "lib/github.com/diku-dk/segmented/segmented"

import "MonoidEq"

module csr (M : MonoidEq) = {
  type elem = M.t
  type matrix = { dims: (i32, i32), vals: []elem, row_ptr: []i32, cols: []i32 }

-- assume row-major
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
}
