fn greet() -> &'static str {
    "Hello, Timpani-n!"
}

fn main() {
    println!("{}", greet());
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_greet() {
        assert_eq!(greet(), "Hello, Timpani-n!");
    }
}
