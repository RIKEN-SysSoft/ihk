function check() {
    if [ $1 == $2 ]; then
	echo "[  OK  ] $2"
    else
	echo "[  NG  ] $2"
    fi
}
