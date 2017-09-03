package main

import (
	"encoding/binary"
	"fmt"
	"io/ioutil"
	"os"
	"syscall"
)

func main() {
	data, err := ioutil.ReadFile(os.Args[1])
	if err != nil {
		panic(err)
	}
	p := 0
	for p < len(data) {
		var stat struct {
			at   syscall.Filetime
			cnt  uint32
			keys []struct {
				code  uint32
				times uint32
			}
		}
		stat.at.LowDateTime = binary.LittleEndian.Uint32(data[p:])
		p += 4
		stat.at.HighDateTime = binary.LittleEndian.Uint32(data[p:])
		p += 4
		stat.cnt = binary.LittleEndian.Uint32(data[p:])
		p += 4
		for i := 0; i < int(stat.cnt); i++ {
			code := binary.LittleEndian.Uint32(data[p:])
			p += 4
			times := binary.LittleEndian.Uint32(data[p:])
			p += 4
			stat.keys = append(stat.keys, struct {
				code  uint32
				times uint32
			}{code, times})
		}
		fmt.Printf("at: %d, cnt: %d\nkeys: %+v\n", stat.at.Nanoseconds(), stat.cnt, stat.keys)
	}
}
