from setuptools import setup

setup(
    name='nfbtools',
    version = "0.2.0",
    packages=[
        "nfbmeter",
        "nfbbootstrap",
        "nfbbootstrap.pypcie",
    ],
    package_dir={
        'nfbmeter': 'nfbmeter',
        'nfbbootstrap': 'nfbbootstrap',
    },
    entry_points={
        'console_scripts': [
            'nfb-meter=nfbmeter.nfbmeter:main',
            'nfb-bootstrap = nfbbootstrap.nfb_bootstrap:main',
        ],
    },
    install_requires=[
        "nfb",
        "pyyaml",
    ]
)
