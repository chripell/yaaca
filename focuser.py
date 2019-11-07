
from astropy.stats import sigma_clipped_stats
from photutils import DAOStarFinder, IRAFStarFinder
from collections import namedtuple
import numpy as np


FocusData = namedtuple('FocusData', 'bot p10 mean p90 top std')


class Focuser:

    def __init__(self, fwhm=3.0, threshold_stds=100., algo='iraf'):
        self.sources = None
        self.n = 0
        self.par = {}
        self.fwhm = fwhm
        self.threshold_stds = threshold_stds
        self.algo = algo
        if self.algo == 'dao':
            self.odata = ("sharpness", "roundness1", "roundness2")
        else:
            self.odata = ("sharpness",)

    def evaluate(self, data):
        mean, median, std = sigma_clipped_stats(data, sigma=3.0, maxiters=5)
        print(mean, median, std)
        if self.algo == 'dao':
            finder = DAOStarFinder(
                fwhm=self.fwhm, threshold=self.threshold_stds*std)
        else:
            finder = IRAFStarFinder(
                fwhm=self.fwhm, threshold=self.threshold_stds*std)
        self.sources = finder(data - median)
        for col in self.sources.colnames:
            self.sources[col].info.format = "%.8g"
        print(self.num())
        if self.num() > 0:
            for p in self.odata:
                self.par[p] = self.calc(p)
        return self.sources

    def num(self):
        if self.sources is not None:
            return len(self.sources)
        return 0

    def calc(self, p):
        data = np.absolute(self.sources.field(p))
        mean = data.mean()
        bot = data.min()
        top = data.max()
        std = data.std()
        p10 = np.percentile(data, 10)
        p90 = np.percentile(data, 90)
        return FocusData(bot, p10, mean, p90, top, std)

    def draw(self, cr, par, radius=10):
        mean = self.par[par].mean
        m_pi = 2 * np.pi
        for i in self.sources:
            if abs(i[par]) >= mean:
                cr.set_source_rgb(0, 1.0, 0)
            else:
                cr.set_source_rgb(1.0, 0, 0)
            cr.arc(i["xcentroid"], i["ycentroid"], radius, 0, m_pi)
            cr.stroke()

    def get(self, p):
        return self.par[p]
