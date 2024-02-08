from setuptools import setup

setup(
    packages=["nfbmeter"],
    package_dir={'nfbmeter': 'nfbmeter'},
    entry_points={
        'console_scripts': [
            'nfb-meter=nfbmeter.nfbmeter:main',
        ],
    },
)
