use alloc::string::String;
use alloc::vec::Vec;
use alloc::boxed::Box;

pub enum Node {
    File { name: String, content: String },
    Dir { name: String, children: Vec<Node> },
}

impl Node {
    fn name(&self) -> &str {
        match self {
            Node::File { name, .. } => name,
            Node::Dir { name, .. } => name,
        }
    }
}

pub struct Vfs {
    root: Node,
    /// Path from root to current directory, as a list of directory names.
    cwd: Vec<String>,
}

pub enum VfsError {
    NotFound,
    NotADirectory,
    AlreadyExists,
    IsADirectory,
}

impl Vfs {
    pub fn new() -> Self {
        Vfs {
            root: Node::Dir { name: String::from("/"), children: Vec::new() },
            cwd: Vec::new(),
        }
    }

    pub fn cwd_path(&self) -> String {
        if self.cwd.is_empty() {
            String::from("/")
        } else {
            let mut s = String::new();
            for part in &self.cwd {
                s.push('/');
                s.push_str(part);
            }
            s
        }
    }

    fn find_dir_mut<'a>(root: &'a mut Node, path: &[String]) -> Option<&'a mut Vec<Node>> {
        let mut current = root;
        for part in path {
            match current {
                Node::Dir { children, .. } => {
                    current = children.iter_mut().find(|n| n.name() == part)?;
                }
                Node::File { .. } => return None,
            }
        }
        match current {
            Node::Dir { children, .. } => Some(children),
            Node::File { .. } => None,
        }
    }

    /// `chd` — change directory. Supports "..", "/", and relative names.
    pub fn chd(&mut self, target: &str) -> Result<(), VfsError> {
        if target == "/" {
            self.cwd.clear();
            return Ok(());
        }
        if target == ".." {
            self.cwd.pop();
            return Ok(());
        }

        let children = Self::find_dir_mut(&mut self.root, &self.cwd)
            .ok_or(VfsError::NotADirectory)?;

        let found = children.iter().find(|n| n.name() == target);
        match found {
            Some(Node::Dir { .. }) => {
                self.cwd.push(String::from(target));
                Ok(())
            }
            Some(Node::File { .. }) => Err(VfsError::NotADirectory),
            None => Err(VfsError::NotFound),
        }
    }

    /// `cre` — create a file or directory in the current directory.
    pub fn cre(&mut self, name: &str, is_dir: bool) -> Result<(), VfsError> {
        let children = Self::find_dir_mut(&mut self.root, &self.cwd)
            .ok_or(VfsError::NotADirectory)?;

        if children.iter().any(|n| n.name() == name) {
            return Err(VfsError::AlreadyExists);
        }

        if is_dir {
            children.push(Node::Dir { name: String::from(name), children: Vec::new() });
        } else {
            children.push(Node::File { name: String::from(name), content: String::new() });
        }
        Ok(())
    }

    /// `con` — content of a file, or a listing if it's a directory.
    pub fn con(&mut self, name: &str) -> Result<Vec<String>, VfsError> {
        let children = Self::find_dir_mut(&mut self.root, &self.cwd)
            .ok_or(VfsError::NotADirectory)?;

        match children.iter().find(|n| n.name() == name) {
            Some(Node::File { content, .. }) => {
                Ok(content.lines().map(String::from).collect())
            }
            Some(Node::Dir { children, .. }) => {
                Ok(children.iter().map(|n| String::from(n.name())).collect())
            }
            None => Err(VfsError::NotFound),
        }
    }

    /// `con` with no argument — list the current directory.
    pub fn con_here(&mut self) -> Vec<String> {
        match Self::find_dir_mut(&mut self.root, &self.cwd) {
            Some(children) => children.iter().map(|n| String::from(n.name())).collect(),
            None => Vec::new(),
        }
    }

    /// `info` — metadata about a file or directory.
    pub fn info(&mut self, name: &str) -> Result<(String, String), VfsError> {
        let children = Self::find_dir_mut(&mut self.root, &self.cwd)
            .ok_or(VfsError::NotADirectory)?;

        match children.iter().find(|n| n.name() == name) {
            Some(Node::File { content, .. }) => {
                Ok((String::from("file"), alloc::format!("{} bytes", content.len())))
            }
            Some(Node::Dir { children, .. }) => {
                Ok((String::from("directory"), alloc::format!("{} items", children.len())))
            }
            None => Err(VfsError::NotFound),
        }
    }

    /// Write content into an existing file (used by the Notes app, etc).
    pub fn write_file(&mut self, name: &str, content: &str) -> Result<(), VfsError> {
        let children = Self::find_dir_mut(&mut self.root, &self.cwd)
            .ok_or(VfsError::NotADirectory)?;

        match children.iter_mut().find(|n| n.name() == name) {
            Some(Node::File { content: c, .. }) => {
                c.clear();
                c.push_str(content);
                Ok(())
            }
            Some(Node::Dir { .. }) => Err(VfsError::IsADirectory),
            None => Err(VfsError::NotFound),
        }
    }
}

lazy_static::lazy_static! {
    pub static ref VFS: spin::Mutex<Vfs> = spin::Mutex::new(Vfs::new());
}