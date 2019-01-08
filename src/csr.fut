import "lib/github.com/diku-dk/sorts/merge_sort"
import "lib/github.com/diku-dk/segmented/segmented"

module csr = {
  type matrix 'a = { dims: (i32, i32), vals: []a, row_ptr: []i32, cols: []i32 }

  -- assume row-major
  let fromList 'a (dims : (i32, i32)) (xs : []((i32,i32),a)): matrix a =
    let sorted_xs = merge_sort (\((x1,y1),_) ((x2,y2),_) -> if x1 == x2 then y1 <= y2 else x1 < x2) xs
    let (idxs, vals) = unzip sorted_xs
    let (rows, cols) = unzip idxs

    let flags = map2 (!=) rows (rotate (-1) rows)
    let row_lens: []i32 = segmented_reduce (+) 0 flags <| replicate (length flags) 1
    let row_ptr : []i32 = scan (\a b -> a + b) 0 row_lens

    in { dims = dims, vals = vals, row_ptr = row_ptr, cols = cols }

  let fromDense [n][m] 'a (ne : a) (eq : a -> a -> bool) (matrix: [n][m]a): matrix a =
    let idxs = replicate m (iota n) |> flatten |> zip (iota m)
    let idxs_mat = zip idxs <| flatten matrix
    let entries = filter (\(_,x) -> !(eq ne x)) idxs_mat
    in fromList (n,m) entries
}
