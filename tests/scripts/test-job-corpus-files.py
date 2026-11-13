"""Compile the actual POSIX scanner and exercise capture-directory selection."""
from pathlib import Path
import subprocess
import tempfile
root=Path(__file__).resolve().parents[2]
source=(root/'port/job_corpus.cpp').read_text()
start=source.index('std::vector<std::string> corpus_files(')
end=source.index('\n}\n',start)+3
with tempfile.TemporaryDirectory(prefix='buzz-corpus-files-') as temp:
    d=Path(temp);capture=d/'captures';capture.mkdir()
    for name in ['job_41_0000.bjc.gz','job_41_0000.bjc.gz.json','bad.bjc.gz.tmp',
                 'corrupt.bjc.gz','notes.json','short']:
        (capture/name).write_text('test selector, not a replay payload')
    c=d/'test.cpp';exe=d/'test'
    c.write_text('#include <dirent.h>\n#include <cstring>\n#include <string>\n#include <vector>\n#include <cstdio>\n'+source[start:end]+'''
int main(int argc,char**argv){if(argc!=2)return 2;for(auto& f:corpus_files(argv[1]))puts(f.c_str());}
''')
    subprocess.run(['c++','-std=c++17','-O2',str(c),'-o',str(exe)],check=True)
    def selected(path):
        return set(subprocess.check_output([str(exe),str(path)],text=True).splitlines())
    assert selected(capture)=={str(capture/'job_41_0000.bjc.gz'),str(capture/'corrupt.bjc.gz')}
    assert selected(capture/'job_41_0000.bjc.gz')=={str(capture/'job_41_0000.bjc.gz')}
    # Explicit files, even invalid ones, still reach the loader and fail there.
    assert selected(capture/'notes.json')=={str(capture/'notes.json')}
    only_metadata=d/'metadata';only_metadata.mkdir();(only_metadata/'a.bjc.gz.json').write_text('{}')
    assert selected(only_metadata)==set()
    print('PASS actual scanner: archive suffixes, metadata exclusion, corrupt archive inclusion, explicit files')
