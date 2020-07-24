#include <iostream>
#include <stdint.h>
#include <vector>
#include<map>
#include <set>
#include<iterator>
using namespace std;
struct Event {
  enum class Type { kTimer, kTaskArrival, kTaskFinish, kIoRequest, kIoEnd };
  struct Task {
    enum class Priority { kHigh, kLow };
    int arrivalTime;
    int deadline;
    Priority priority;
    int taskId;
  };
  Type type;
  int time;
  Task task;
};

struct Action {
  int cpuTask, ioTask;
};

auto cmp=[](Event::Task left, Event::Task right ){return left.deadline>right.deadline;};

struct task_node {
  Event::Task value;
  task_node *next;
  task_node *prev;
};

class task_queue {
public:
  task_node *head;
  task_node *min;
  map<int, task_node *> m;
  void add(Event::Task _task);
  int get_min();
  void remove(int taskid);
  bool abandon(int time);
  task_queue();
};
task_queue::task_queue() {
  head = NULL;
  min=NULL;
}
void task_queue::add(Event::Task _task) {
  if (head == NULL){
    head = new task_node;
    head->value = _task;
    head->next = head->prev = NULL;
    m[_task.taskId] = head;
    min=head;
    return;
  }
  task_node *newnode=new task_node;
  newnode->next = head;
  newnode->prev = NULL;
  newnode->value = _task;
  head->prev = newnode;
  m[_task.taskId] = newnode;
  head=newnode;
  if ((newnode->value).deadline < (min->value).deadline)
    min = newnode;
  
}
void task_queue::remove(int taskid) {
  task_node *temp = m[taskid];
  if (temp == NULL)
    return;
  if (temp->next == NULL && temp->prev == NULL) {
    delete temp;
    m.erase(taskid);
    head = min = NULL;
    return;
  }
  if (temp == min) {
    min->value.deadline = INT32_MAX;
    task_node *temp2=head;
    while (temp2 != NULL) {
      if (temp2->value.deadline < min->value.deadline)
        min = temp2;
      temp2=temp2->next;
    }
  }
  if (temp->next != NULL)
    (temp->next)->prev = temp->prev;
  if (temp->prev != NULL)
    (temp->prev)->next = temp->next;

  delete temp;
  m.erase(taskid);
}
int task_queue::get_min() {
  if (min == NULL)
    return 0;
  return (min->value).taskId;
  }
bool task_queue::abandon(int time) {
  if (head == NULL)
    return false;
  while ((head->value).deadline-time<=0 && head->next != NULL) {
    head = head->next;
    m[((head->prev)->value).taskId]=NULL;
    delete (head->prev);
    head->prev = NULL;
  }
  if ((head->value).deadline-time <=0)
    delete head;
  return (head!=NULL);
}
task_queue hioqueue,lioqueue,hcpuqueue,lcpuqueue;
Action policy(const std::vector<Event> &events, int currentCpuTask,
              int currentIoTask) {
  int time = events[0].time;
  int cpu_task = currentCpuTask;
  int io_task = currentIoTask;
  int i;
  for (i = 0; i < events.size(); i++) {
    if (events[i].type == Event::Type::kTimer)
      continue;
    if (events[i].type == Event::Type::kTaskArrival) {
      if(events[i].task.priority==Event::Task::Priority::kHigh)
        hcpuqueue.add(events[i].task);
      else
        lcpuqueue.add(events[i].task);
    }
    if (events[i].type == Event::Type::kTaskFinish) {
      if(events[i].task.priority==Event::Task::Priority::kHigh)
        hcpuqueue.remove(events[i].task.taskId);
      else
        lcpuqueue.remove(events[i].task.taskId);
    }
    if (events[i].type == Event::Type::kIoRequest) {
      if(events[i].task.priority==Event::Task::Priority::kHigh){
        hcpuqueue.remove(events[i].task.taskId);
        hioqueue.add(events[i].task);
      } else {
        lcpuqueue.remove(events[i].task.taskId);
        lioqueue.add(events[i].task);
      }
    }
    if (events[i].type == Event::Type::kIoEnd) {
      if(events[i].task.priority==Event::Task::Priority::kHigh){
        hioqueue.remove(events[i].task.taskId);
        hcpuqueue.add(events[i].task);
      }else {
        lioqueue.remove(events[i].task.taskId);
        lcpuqueue.add(events[i].task);
      }
    }
  }
  if (hcpuqueue.head == NULL && lcpuqueue.head == NULL)
    cpu_task = currentCpuTask;
  else if (hcpuqueue.head)
    cpu_task = hcpuqueue.get_min();
  else
    cpu_task = lcpuqueue.get_min();
  if (currentIoTask || (hioqueue.head == NULL && lioqueue.head == NULL))
    io_task = currentIoTask;
  else if (hioqueue.head)
    io_task = hioqueue.get_min();
  else
    io_task = lioqueue.get_min();
  Action ans;
  ans.cpuTask = cpu_task;
  ans.ioTask=io_task;
  return ans;

}