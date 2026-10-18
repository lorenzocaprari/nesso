// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

// Package runner executes the nesso binary and captures its observable
// behaviour.
package runner

import (
	"bytes"
	"context"
	"errors"
	"fmt"
	"io"
	"os"
	"os/exec"
	"strings"
	"syscall"
	"time"
	"unsafe"
)

// Timeout bounds a single nesso invocation.
const Timeout = 120 * time.Second

// Result is the raw outcome of one invocation.
type Result struct {
	ExitCode int
	Stdout   string
	Stderr   string
}

// Run executes binary with args in dir. A non-zero exit status is a Result, not
// an error. Failing to start the process or exceeding Timeout is an error.
func Run(binary string, args []string, dir string, stdin string, env map[string]string, terminal bool) (Result, error) {
	ctx, cancel := context.WithTimeout(context.Background(), Timeout)
	defer cancel()

	var stdout, stderr bytes.Buffer
	cmd := exec.CommandContext(ctx, binary, args...)
	cmd.Dir = dir
	cmd.Stdout = &stdout
	var master, slave *os.File
	if terminal {
		var ptyErr error
		master, slave, ptyErr = openPty()
		if ptyErr != nil {
			return Result{}, ptyErr
		}
		defer func() {
			if master != nil {
				master.Close()
			}
		}()
		cmd.Stderr = slave
	} else {
		cmd.Stderr = &stderr
	}
	if len(env) > 0 {
		cmd.Env = os.Environ()
		for key, value := range env {
			cmd.Env = append(cmd.Env, key+"="+value)
		}
	}
	if stdin != "" {
		cmd.Stdin = strings.NewReader(stdin)
	}
	if err := cmd.Start(); err != nil {
		if slave != nil {
			slave.Close()
		}
		return Result{}, fmt.Errorf("running %s: %w", binary, err)
	}
	var copied <-chan struct{}
	if slave != nil {
		slave.Close()
		done := make(chan struct{})
		go func() {
			_, _ = io.Copy(&stderr, master)
			close(done)
		}()
		copied = done
	}
	runErr := cmd.Wait()
	if master != nil {
		master.Close()
		master = nil
		<-copied
	}

	result := Result{Stdout: stdout.String(), Stderr: stderr.String()}
	var exitErr *exec.ExitError
	switch {
	case runErr == nil:
		return result, nil
	case errors.As(runErr, &exitErr) && ctx.Err() == nil:
		result.ExitCode = exitErr.ExitCode()
		return result, nil
	default:
		return result, fmt.Errorf("running %s: %w", binary, runErr)
	}
}

// openPty returns a master/slave pair so the child sees a terminal on stderr.
// Progress reporting is gated on isatty, which a pipe never satisfies.
func openPty() (master *os.File, slave *os.File, err error) {
	master, err = os.OpenFile("/dev/ptmx", os.O_RDWR, 0)
	if err != nil {
		return nil, nil, fmt.Errorf("opening /dev/ptmx: %w", err)
	}
	// Linux ptmx: unlock, then read the slave index. grantpt is unnecessary
	// when the opener and the slave share a user.
	var locked int32
	if errno := ioctl(master, 0x40045431, uintptr(unsafe.Pointer(&locked))); errno != nil {
		master.Close()
		return nil, nil, fmt.Errorf("unlocking pty: %w", errno)
	}
	var index uint32
	if errno := ioctl(master, 0x80045430, uintptr(unsafe.Pointer(&index))); errno != nil {
		master.Close()
		return nil, nil, fmt.Errorf("reading pty index: %w", errno)
	}
	slave, err = os.OpenFile(fmt.Sprintf("/dev/pts/%d", index), os.O_RDWR|syscall.O_NOCTTY, 0)
	if err != nil {
		master.Close()
		return nil, nil, fmt.Errorf("opening pty slave: %w", err)
	}
	return master, slave, nil
}

func ioctl(file *os.File, request uintptr, arg uintptr) error {
	_, _, errno := syscall.Syscall(syscall.SYS_IOCTL, file.Fd(), request, arg)
	if errno != 0 {
		return errno
	}
	return nil
}
