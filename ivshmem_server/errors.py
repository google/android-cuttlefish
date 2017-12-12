
class VersionException(BaseException):
  def __init__(self):
    pass

  def __str__(self):
    return "Python 3 needed to run this program"
