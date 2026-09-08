import types
import unittest
from mechbot_http import validate_origin


class OriginTest(unittest.TestCase):
    def test_dashboard_and_local_scripts(self):
        for headers in ({}, {'Host': 'robot.local:8765', 'Origin': 'http://robot.local:8765'},
                        {'Host': 'ROBOT.LOCAL', 'Origin': 'https://robot.local'}):
            validate_origin(types.SimpleNamespace(headers=headers))

    def test_other_websites_cannot_post_actions(self):
        bad = ['null', 'http://example.com', 'https://robot.local.evil.test',
               'http://robot.local:1234', 'http://user@robot.local', 'file://robot.local',
               'http://robot.local/path', 'http://robot.local?query', 'http://robot.local#fragment']
        for origin in bad:
            with self.subTest(origin=origin), self.assertRaises(ValueError):
                validate_origin(types.SimpleNamespace(headers={'Host': 'robot.local', 'Origin': origin}))
        with self.assertRaises(ValueError):
            validate_origin(types.SimpleNamespace(headers={'Sec-Fetch-Site': 'cross-site'}))


if __name__ == '__main__':
    unittest.main()
