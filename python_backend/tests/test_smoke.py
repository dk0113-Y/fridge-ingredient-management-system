import unittest


class SmokeTestCase(unittest.TestCase):
    def test_placeholder(self):
        self.assertTrue("python_backend placeholder".startswith("python_backend"))


if __name__ == "__main__":
    unittest.main()
