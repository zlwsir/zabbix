/*
** Zabbix
** Copyright (C) 2001-2019 Zabbix SIA
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation; either version 2 of the License, or
** (at your option) any later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program; if not, write to the Free Software
** Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
**/

package scheduler

import (
	"reflect"
	"time"
	"zabbix/internal/plugin"
	"zabbix/pkg/itemutil"
	"zabbix/pkg/log"
)

// task priority within the same second is done by setting nanosecond component
const (
	priorityStarterTaskNs = iota
	priorityCollectorTaskNs
	priorityWatcherTaskNs
	priorityExporterTaskNs
	priorityStopperTaskNs
)

type Task struct {
	plugin    *Plugin
	scheduled time.Time
	index     int
	active    bool
}

func (t *Task) Plugin() *Plugin {
	return t.plugin
}

func (t *Task) Scheduled() time.Time {
	return t.scheduled
}

func (t *Task) Weight() int {
	return 1
}

func (t *Task) Index() int {
	return t.index
}

func (t *Task) SetIndex(index int) {
	t.index = index
}

func (t *Task) Deactivate() {
	if t.index != -1 {
		t.plugin.removeTask(t.index)
	}
	t.active = false
}

func (t *Task) Active() bool {
	return t.active
}

type CollectorTask struct {
	Task
}

func (t *CollectorTask) Perform(s Scheduler) {
	go func() {
		collector, _ := t.plugin.impl.(plugin.Collector)
		if err := collector.Collect(); err != nil {
			log.Warningf("Plugin '%s' collector failed: %s", t.plugin.impl.Name(), err.Error())
		}
		s.FinishTask(t)
	}()
}

func (t *CollectorTask) Reschedule() bool {
	collector, _ := t.plugin.impl.(plugin.Collector)
	t.scheduled = t.scheduled.Add(time.Duration(collector.Period()) * time.Second)
	return true
}

func (t *CollectorTask) Weight() int {
	return t.plugin.capacity
}

type ExporterTask struct {
	Task
	writer plugin.ResultWriter
	item   *Item
}

func (t *ExporterTask) Perform(s Scheduler) {
	go func(itemkey string) {
		exporter, _ := t.plugin.impl.(plugin.Exporter)
		now := time.Now()
		var key string
		var params []string
		var err error
		if key, params, err = itemutil.ParseKey(itemkey); err == nil {
			var ret interface{}
			if ret, err = exporter.Export(key, params); err == nil {
				rt := reflect.TypeOf(ret)
				switch rt.Kind() {
				case reflect.Slice:
					fallthrough
				case reflect.Array:
					s := reflect.ValueOf(ret)
					for i := 0; i < s.Len(); i++ {
						value := itemutil.ValueToString(s.Index(i))
						t.writer.Write(&plugin.Result{Itemid: t.item.itemid, Value: &value, Ts: now})
					}
				default:
					value := itemutil.ValueToString(ret)
					t.writer.Write(&plugin.Result{Itemid: t.item.itemid, Value: &value, Ts: now})
				}
			}
		}
		if err != nil {
			t.writer.Write(&plugin.Result{Itemid: t.item.itemid, Error: err, Ts: now})
		}
		s.FinishTask(t)
	}(t.item.key)
}

func (t *ExporterTask) Reschedule() bool {
	t.scheduled, _ = itemutil.GetNextcheck(t.item.itemid, t.item.delay, t.item.unsupported, time.Now())
	return true
}

type StarterTask struct {
	Task
}

func (t *StarterTask) Perform(s Scheduler) {
	go func() {
		runner, _ := t.plugin.impl.(plugin.Runner)
		runner.Start()
		s.FinishTask(t)
	}()
}

func (t *StarterTask) Reschedule() bool {
	return false
}

func (t *StarterTask) Weight() int {
	return t.plugin.capacity
}

type StopperTask struct {
	Task
}

func (t *StopperTask) Perform(s Scheduler) {
	go func() {
		runner, _ := t.plugin.impl.(plugin.Runner)
		runner.Stop()
		s.FinishTask(t)
	}()
}

func (t *StopperTask) Reschedule() bool {
	return false
}

func (t *StopperTask) Weight() int {
	return t.plugin.capacity
}

type WatcherTask struct {
	Task
	requests []*plugin.Request
	sink     plugin.ResultWriter
}

func (t *WatcherTask) Perform(s Scheduler) {
	go func() {
		watcher, _ := t.plugin.impl.(plugin.Watcher)
		watcher.Watch(t.requests, t.sink)
		s.FinishTask(t)
	}()
}

func (t *WatcherTask) Reschedule() bool {
	return false
}

func (t *WatcherTask) Weight() int {
	return t.plugin.capacity
}
