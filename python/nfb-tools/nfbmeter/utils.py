class NoCurses():
    def __init__(self):
        self._buffers = []

    def getmaxyx(self):
        #return termios.tcgetwinsize(fd)
        return 64, 180

    def refresh(self):
        for line in self._buffers:
            print(line.rstrip())

    def addstr(self, row, col, text):
        y, x = self.getmaxyx()
        while row >= len(self._buffers):
            self._buffers.append(" " * x)

        orig = self._buffers[row]
        self._buffers[row] = orig[:col] + text + orig[col+len(text):]

def nocurses_wrapper(fn, *args, **kwargs):
    fn(NoCurses(), *args, **kwargs)
