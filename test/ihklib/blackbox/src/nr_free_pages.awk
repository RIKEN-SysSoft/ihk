BEGIN {
    id = -1;
    "getconf PAGE_SIZE" | getline page_size;
}

/Node .*, zone\s*(Normal|DMA32)/ {
    id = substr($2, 1, length($2) - 1);
}

{
    if ($1 == "nr_free_pages" && id != -1) {
	#printf("id: %d, nr_free_pages: %ld\n", id, $2);

	sizes[id] += $2 * page_size;
	#printf("id: %d, sizes: %ld\n", id, sizes[id]);
	max_id = id;
	id = -1;
    }
}

END {
    if (max_id > 3) {
	min_id = max_id - 3;
    } else {
	min_id = 0;
    }
    for (i = min_id; i <= max_id; i++) {
	printf("%d ", sizes[i]);
    }
}

