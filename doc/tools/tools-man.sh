cat $1.rst | sed '
	s/.. option:: //;
	s/.. rubric:: //;
	s/:program://;
	s/:manpage://
' | rst2man -q > $1.1
