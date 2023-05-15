import sys
import subprocess

def generate_doxygen():
    """
    Generate doxygen documentation
    """
    try:
        print('Generating doxygen documentation...', end='')
        subprocess.run('doxygen Doxyfile', shell=True, check=True)
        print('done')
    except subprocess.CalledProcessError:
        print('Error: doxygen failed to generate documentation')
        sys.exit(1)
        
generate_doxygen()