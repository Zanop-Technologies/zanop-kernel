use alloc::collections::VecDeque;
use core::sync::atomic::{AtomicU64, Ordering};
use spin::Mutex;

static NEXT_TASK_ID: AtomicU64 = AtomicU64::new(0);

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum TaskState {
    Ready,
    Running,
    Blocked,
    Terminated,
}

pub struct Task {
    pub id: u64,
    pub state: TaskState,
    // TODO: saved register context / stack pointer for context switching
    // pub context: TaskContext,
}

impl Task {
    pub fn new() -> Self {
        Task {
            id: NEXT_TASK_ID.fetch_add(1, Ordering::Relaxed),
            state: TaskState::Ready,
        }
    }
}

pub struct Scheduler {
    tasks: VecDeque<Task>,
    current: Option<Task>,
}

impl Scheduler {
    const fn new() -> Self {
        Scheduler {
            tasks: VecDeque::new(),
            current: None,
        }
    }

    pub fn spawn(&mut self, task: Task) {
        self.tasks.push_back(task);
    }

    pub fn schedule(&mut self) -> Option<&Task> {
        // TODO: real scheduling policy (round-robin to start)
        // TODO: actual context switch (save/restore registers, switch stacks)
        if let Some(next) = self.tasks.pop_front() {
            if let Some(prev) = self.current.take() {
                if prev.state != TaskState::Terminated {
                    self.tasks.push_back(prev);
                }
            }
            self.current = Some(next);
        }
        self.current.as_ref()
    }
}

pub static SCHEDULER: Mutex<Scheduler> = Mutex::new(Scheduler::new());

pub fn init() {
    // no tasks spawned yet — kernel runs single-threaded until
    // the first task is created and context switching is wired up
}