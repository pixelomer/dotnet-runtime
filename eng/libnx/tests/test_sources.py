"""Exercise dependency integrity and preservation of local work without network access."""
import importlib.util
import io
from pathlib import Path
import subprocess
import tarfile
import tempfile
import unittest

helper = Path(__file__).resolve().parents[1] / 'support.py'
spec = importlib.util.spec_from_file_location('source_support_tested', helper)
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)

class Sources(unittest.TestCase):
    def test_pinned_fetch_rejects_dirty_or_different_revision(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            upstream = root / 'upstream'
            subprocess.run(['git', 'init', '-q', str(upstream)], check=True)
            (upstream / 'input').write_text('original')
            subprocess.run(['git', '-C', str(upstream), 'add', '.'], check=True)
            subprocess.run(['git', '-C', str(upstream), '-c', 'user.name=GPT-6 Astra',
                '-c', 'user.email=noreply@openai.com', 'commit', '-qm', 'fixture'], check=True)
            revision = subprocess.check_output(['git', '-C', str(upstream), 'rev-parse', 'HEAD'], text=True).strip()
            source = {'url': 'https://example.invalid/source.git', 'revision': revision}
            target = m.git_source(source, root / 'checkout', {source['url']: str(upstream)})
            self.assertEqual((target / 'input').read_text(), 'original')
            self.assertEqual(m.git_source(source, target), target)
            (target / 'input').write_text('keep my edits')
            with self.assertRaises(RuntimeError): m.git_source(source, target)
            self.assertEqual((target / 'input').read_text(), 'keep my edits')
            (target / 'input').write_text('original')
            with self.assertRaises(RuntimeError): m.git_source(source | {'revision': '0' * 40}, target)
            with self.assertRaises(ValueError): m.git_source(source | {'revision': 'main'}, target)

    def test_bad_download_cannot_replace_verified_file(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            original = root / 'original'
            original.write_bytes(b'good')
            source = {'url': original.as_uri(), 'sha256': m.digest(original)}
            target = m.download(source, root / 'cache')
            self.assertEqual(target.read_bytes(), b'good')
            original.write_bytes(b'bad')
            self.assertEqual(m.download(source, target).read_bytes(), b'good')
            with self.assertRaises(RuntimeError):
                m.download(source, root / 'bad-cache')
            self.assertFalse((root / 'bad-cache').exists())

    def test_tar_traversal_is_rejected(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            archive = root / 'bad.tar'
            with tarfile.open(archive, 'w') as tar:
                item = tarfile.TarInfo('../escape')
                item.size = 1
                tar.addfile(item, io.BytesIO(b'x'))
            with self.assertRaises(tarfile.FilterError):
                m.extract_tar(archive, root / 'extracted')
            self.assertFalse((root / 'escape').exists())

if __name__ == '__main__': unittest.main()
