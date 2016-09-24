#!/usr/bin/env python2

import re
import os
import struct
import numpy as np

try:
    import scipy.stats as stats
    import scipy.ndimage as ndi
    import scipy.linalg as linalg
    from scipy.ndimage import (
        gaussian_filter, generate_binary_structure, binary_erosion, label)
except ImportError:
    print "Cannot import scipy libraries"

try:
    import pywt
except ImportError:
    print "Cannot import wavelet libraries"

myfloat = np.float32
gamma = 1.0 / 2.2

imtype_help = "image type: 0:ppm, 1:pgm, 2:read ppm and convert to pgm, 3:8bit ppm, 4:8bit pgm, 5:ppm, 8:to 16, 6:pgm 8 to 16 7:pgm debayer 16 8:pgm debayer 8"
expand_file_list_help = "[files or @file_list ...]"

def read_pgm(filename, byteorder='>'):
    with open(filename, 'rb') as f:
        buffer = f.read()
    try:
        header, width, height, maxval = re.search(
            b"(^P5\s(?:\s*#.*[\r\n])*"
            b"(\d+)\s(?:\s*#.*[\r\n])*"
            b"(\d+)\s(?:\s*#.*[\r\n])*"
            b"(\d+)(?:[ \t]*#.*)*[\r\n])", buffer).groups()
    except AttributeError:
        raise ValueError("Not a raw PGM file: '%s'" % filename)
    r = np.frombuffer(buffer,
                      dtype='u1' if int(maxval) < 256 else byteorder+'u2',
                      count=int(width)*int(height),
                      offset=len(header)
                      ).reshape((int(height), int(width)))
    return r.transpose()

def write_pgm_255(filename, arr):
    arr = arr.clip(0, 255)
    narr = arr.astype(np.uint8)
    narr = narr.transpose()
    sarr = narr.shape
    f = open(filename,'w')
    f.write('P5\n%d %d\n255\n'%(sarr[1], sarr[0]))
    narr.tofile(f)
    f.close()

def swab(arr):
    h = arr / 256
    l = arr % 256
    return h + l * 256

def write_pgm_65535(filename, arr):
    arr = arr.clip(0, 65535)
    narr = arr.astype(np.uint16)
    narr = narr.transpose()
    narr = swab(narr)
    sarr = narr.shape
    f = open(filename,'w')
    f.write('P5\n%d %d\n65535\n'%(sarr[1], sarr[0]))
    narr.tofile(f)
    f.close()

def waveletDenoise(u,noiseSigma):
    wavelet = pywt.Wavelet('bior6.8')
    levels  = int( np.log2(u.shape[0]) )
    waveletCoeffs = pywt.wavedec2( u, wavelet, level=levels)
    threshold=noiseSigma*np.sqrt(2*np.log2(u.size))
    NWC = map(lambda x: pywt.thresholding.soft(x,threshold), waveletCoeffs)
    u = pywt.waverec2( NWC, wavelet)[:u.shape[0],:u.shape[1]]
    return u

def read_ppm(filename, byteorder='>'):
    with open(filename, 'rb') as f:
        buffer = f.read()
    try:
        header, width, height, maxval = re.search(
            b"(^P6\s(?:\s*#.*[\r\n])*"
            b"(\d+)\s(?:\s*#.*[\r\n])*"
            b"(\d+)\s(?:\s*#.*[\r\n])*"
            b"(\d+)\s(?:\s*#.*[\r\n]\s)*)", buffer).groups()
    except AttributeError:
        raise ValueError("Not a raw PPM file: '%s'" % filename)
    arr = np.frombuffer(buffer,
                        dtype='u1' if int(maxval) < 256 else byteorder+'u2',
                        count=int(width)*int(height)*3,
                        offset=len(header)
                        ).reshape((int(height), int(width), 3))
    r = [arr[:,:,0], arr[:,:,1], arr[:,:,2]]
    return [x.transpose() for x in r]

def write_ppm_255(filename, imm):
    imm = [x.clip(0, 255) for x in imm]
    arr = np.dstack(imm)
    narr = arr.astype(np.uint8)
    narr = narr.transpose((1,0,2))
    sarr = narr.shape
    f = open(filename,'w')
    f.write('P6\n%d %d\n255\n'%(sarr[1], sarr[0]))
    narr.tofile(f)
    f.close()

def write_ppm_65535(filename, imm):
    imm = [x.clip(0, 65535) for x in imm]
    arr = np.dstack(imm)
    narr = arr.astype(np.uint16)
    narr = narr.transpose((1,0,2))
    narr = swab(narr)
    sarr = narr.shape
    f = open(filename,'w')
    f.write('P6\n%d %d\n65535\n'%(sarr[1], sarr[0]))
    narr.tofile(f)
    f.close()

'''canny.py - Canny Edge detector
Copyright (C) 2011, the scikits-image team
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:

 1. Redistributions of source code must retain the above copyright
    notice, this list of conditions and the following disclaimer.
 2. Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in
    the documentation and/or other materials provided with the
    distribution.
 3. Neither the name of skimage nor the names of its contributors may be
    used to endorse or promote products derived from this software without
    specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.

Reference: Canny, J., A Computational Approach To Edge Detection, IEEE Trans.
    Pattern Analysis and Machine Intelligence, 8:679-714, 1986
'''
def smooth_with_function_and_mask(image, function, mask):
    """Smooth an image with a linear function, ignoring masked pixels

    Parameters
    ----------
    image : array
      The image to smooth

    function : callable
      A function that takes an image and returns a smoothed image

    mask : array
      Mask with 1's for significant pixels, 0 for masked pixels

    Notes
    ------
    This function calculates the fractional contribution of masked pixels
    by applying the function to the mask (which gets you the fraction of
    the pixel data that's due to significant points). We then mask the image
    and apply the function. The resulting values will be lower by the
    bleed-over fraction, so you can recalibrate by dividing by the function
    on the mask to recover the effect of smoothing from just the significant
    pixels.
    """
    bleed_over = function(mask.astype(float))
    masked_image = np.zeros(image.shape, image.dtype)
    masked_image[mask] = image[mask]
    smoothed_image = function(masked_image)
    output_image = smoothed_image / (bleed_over + np.finfo(float).eps)
    return output_image


def canny(image, sigma=1., low_threshold=.1, high_threshold=.2, mask=None):
    '''Edge filter an image using the Canny algorithm.

    Parameters
    -----------
    image : array_like, dtype=float
      The greyscale input image to detect edges on; should be normalized to
      0.0 to 1.0.

    sigma : float
      The standard deviation of the Gaussian filter

    low_threshold : float
      The lower bound for hysterisis thresholding (linking edges)

    high_threshold : float
      The upper bound for hysterisis thresholding (linking edges)

    mask : array, dtype=bool, optional
      An optional mask to limit the application of Canny to a certain area.

    Returns
    -------
    output : array (image)
      The binary edge map.

    See also
    --------
    skimage.sobel

    Notes
    -----
    The steps of the algorithm are as follows:

    * Smooth the image using a Gaussian with ``sigma`` width.

    * Apply the horizontal and vertical Sobel operators to get the gradients
      within the image. The edge strength is the norm of the gradient.

    * Thin potential edges to 1-pixel wide curves. First, find the normal
      to the edge at each point. This is done by looking at the
      signs and the relative magnitude of the X-Sobel and Y-Sobel
      to sort the points into 4 categories: horizontal, vertical,
      diagonal and antidiagonal. Then look in the normal and reverse
      directions to see if the values in either of those directions are
      greater than the point in question. Use interpolation to get a mix of
      points instead of picking the one that's the closest to the normal.

    * Perform a hysteresis thresholding: first label all points above the
      high threshold as edges. Then recursively label any point above the
      low threshold that is 8-connected to a labeled point as an edge.

    References
    -----------
    Canny, J., A Computational Approach To Edge Detection, IEEE Trans.
    Pattern Analysis and Machine Intelligence, 8:679-714, 1986

    William Green' Canny tutorial
    http://dasl.mem.drexel.edu/alumni/bGreen/www.pages.drexel.edu/_weg22/can_tut.html

    Examples
    --------
    >>> from skimage import filter
    >>> # Generate noisy image of a square
    >>> im = np.zeros((256, 256))
    >>> im[64:-64, 64:-64] = 1
    >>> im += 0.2*np.random.random(im.shape)
    >>> # First trial with the Canny filter, with the default smoothing
    >>> edges1 = filter.canny(im)
    >>> # Increase the smoothing for better results
    >>> edges2 = filter.canny(im, sigma=3)
    '''

    #
    # The steps involved:
    #
    # * Smooth using the Gaussian with sigma above.
    #
    # * Apply the horizontal and vertical Sobel operators to get the gradients
    #   within the image. The edge strength is the sum of the magnitudes
    #   of the gradients in each direction.
    #
    # * Find the normal to the edge at each point using the arctangent of the
    #   ratio of the Y sobel over the X sobel - pragmatically, we can
    #   look at the signs of X and Y and the relative magnitude of X vs Y
    #   to sort the points into 4 categories: horizontal, vertical,
    #   diagonal and antidiagonal.
    #
    # * Look in the normal and reverse directions to see if the values
    #   in either of those directions are greater than the point in question.
    #   Use interpolation to get a mix of points instead of picking the one
    #   that's the closest to the normal.
    #
    # * Label all points above the high threshold as edges.
    # * Recursively label any point above the low threshold that is 8-connected
    #   to a labeled point as an edge.
    #
    # Regarding masks, any point touching a masked point will have a gradient
    # that is "infected" by the masked point, so it's enough to erode the
    # mask by one and then mask the output. We also mask out the border points
    # because who knows what lies beyond the edge of the image?
    #

    if image.ndim != 2:
        raise TypeError("The input 'image' must be a two dimensional array.")

    if mask is None:
        mask = np.ones(image.shape, dtype=bool)
    fsmooth = lambda x: gaussian_filter(x, sigma, mode='constant')
    smoothed = smooth_with_function_and_mask(image, fsmooth, mask)
    jsobel = ndi.sobel(smoothed, axis=1)
    isobel = ndi.sobel(smoothed, axis=0)
    abs_isobel = np.abs(isobel)
    abs_jsobel = np.abs(jsobel)
    magnitude = np.hypot(isobel, jsobel)

    #
    # Make the eroded mask. Setting the border value to zero will wipe
    # out the image edges for us.
    #
    s = generate_binary_structure(2, 2)
    eroded_mask = binary_erosion(mask, s, border_value=0)
    eroded_mask = eroded_mask & (magnitude > 0)
    #
    #--------- Find local maxima --------------
    #
    # Assign each point to have a normal of 0-45 degrees, 45-90 degrees,
    # 90-135 degrees and 135-180 degrees.
    #
    local_maxima = np.zeros(image.shape, bool)
    #----- 0 to 45 degrees ------
    pts_plus = (isobel >= 0) & (jsobel >= 0) & (abs_isobel >= abs_jsobel)
    pts_minus = (isobel <= 0) & (jsobel <= 0) & (abs_isobel >= abs_jsobel)
    pts = pts_plus | pts_minus
    pts = eroded_mask & pts
    # Get the magnitudes shifted left to make a matrix of the points to the
    # right of pts. Similarly, shift left and down to get the points to the
    # top right of pts.
    c1 = magnitude[1:, :][pts[:-1, :]]
    c2 = magnitude[1:, 1:][pts[:-1, :-1]]
    m = magnitude[pts]
    w = abs_jsobel[pts] / abs_isobel[pts]
    c_plus = c2 * w + c1 * (1 - w) <= m
    c1 = magnitude[:-1, :][pts[1:, :]]
    c2 = magnitude[:-1, :-1][pts[1:, 1:]]
    c_minus = c2 * w + c1 * (1 - w) <= m
    local_maxima[pts] = c_plus & c_minus
    #----- 45 to 90 degrees ------
    # Mix diagonal and vertical
    #
    pts_plus = (isobel >= 0) & (jsobel >= 0) & (abs_isobel <= abs_jsobel)
    pts_minus = (isobel <= 0) & (jsobel <= 0) & (abs_isobel <= abs_jsobel)
    pts = pts_plus | pts_minus
    pts = eroded_mask & pts
    c1 = magnitude[:, 1:][pts[:, :-1]]
    c2 = magnitude[1:, 1:][pts[:-1, :-1]]
    m = magnitude[pts]
    w = abs_isobel[pts] / abs_jsobel[pts]
    c_plus = c2 * w + c1 * (1 - w) <= m
    c1 = magnitude[:, :-1][pts[:, 1:]]
    c2 = magnitude[:-1, :-1][pts[1:, 1:]]
    c_minus = c2 * w + c1 * (1 - w) <= m
    local_maxima[pts] = c_plus & c_minus
    #----- 90 to 135 degrees ------
    # Mix anti-diagonal and vertical
    #
    pts_plus = (isobel <= 0) & (jsobel >= 0) & (abs_isobel <= abs_jsobel)
    pts_minus = (isobel >= 0) & (jsobel <= 0) & (abs_isobel <= abs_jsobel)
    pts = pts_plus | pts_minus
    pts = eroded_mask & pts
    c1a = magnitude[:, 1:][pts[:, :-1]]
    c2a = magnitude[:-1, 1:][pts[1:, :-1]]
    m = magnitude[pts]
    w = abs_isobel[pts] / abs_jsobel[pts]
    c_plus = c2a * w + c1a * (1.0 - w) <= m
    c1 = magnitude[:, :-1][pts[:, 1:]]
    c2 = magnitude[1:, :-1][pts[:-1, 1:]]
    c_minus = c2 * w + c1 * (1.0 - w) <= m
    local_maxima[pts] = c_plus & c_minus
    #----- 135 to 180 degrees ------
    # Mix anti-diagonal and anti-horizontal
    #
    pts_plus = (isobel <= 0) & (jsobel >= 0) & (abs_isobel >= abs_jsobel)
    pts_minus = (isobel >= 0) & (jsobel <= 0) & (abs_isobel >= abs_jsobel)
    pts = pts_plus | pts_minus
    pts = eroded_mask & pts
    c1 = magnitude[:-1, :][pts[1:, :]]
    c2 = magnitude[:-1, 1:][pts[1:, :-1]]
    m = magnitude[pts]
    w = abs_jsobel[pts] / abs_isobel[pts]
    c_plus = c2 * w + c1 * (1 - w) <= m
    c1 = magnitude[1:, :][pts[:-1, :]]
    c2 = magnitude[1:, :-1][pts[:-1, 1:]]
    c_minus = c2 * w + c1 * (1 - w) <= m
    local_maxima[pts] = c_plus & c_minus
    #
    #---- Create two masks at the two thresholds.
    #
    high_mask = local_maxima & (magnitude >= high_threshold)
    low_mask = local_maxima & (magnitude >= low_threshold)
    #
    # Segment the low-mask, then only keep low-segments that have
    # some high_mask component in them
    #
    labels, count = label(low_mask, np.ndarray((3, 3), bool))
    if count == 0:
        return low_mask

    sums = (np.array(ndi.sum(high_mask, labels,
                             np.arange(count, dtype=np.int32) + 1),
                     copy=False, ndmin=1))
    good_label = np.zeros((count + 1,), bool)
    good_label[1:] = sums > 0
    output_mask = good_label[labels]
    return output_mask

def registration_dft(buf1ft,buf2ft):
        (m,n)=buf1ft.shape
        CC = np.abs(np.fft.ifft2(buf1ft * np.conj(buf2ft)))
        (rloc,cloc) = np.unravel_index(CC.argmax(), CC.shape)
        md2 = int(m/2)
        nd2 = int(n/2)
        if rloc > md2:
            row_shift = rloc - m
        else:
            row_shift = rloc
        if cloc > nd2:
            col_shift = cloc - n
        else:
            col_shift = cloc
        return [row_shift,col_shift]

#Find the geometric median of an image
def geometric_median(nim,threshold=None,tolerance=0.1,iterations=10):
    if threshold==None:
        threshold=np.min(nim)
    msk = nim>threshold
    xx,yy = np.meshgrid(xrange(nim.shape[1]),xrange(nim.shape[0]))

    gmx,gmy=np.mean(xx[msk]), np.mean(yy[msk])
    for i in range(iterations):
        gmxNew = np.sum(xx[msk]/np.sqrt( (xx[msk]-gmx)**2 + (yy[msk]-gmy)**2))
        gmxNew = gmxNew / np.sum(1./np.sqrt( (xx[msk]-gmx)**2 + (yy[msk]-gmy)**2))
        gmyNew = np.sum(yy[msk]/np.sqrt( (xx[msk]-gmx)**2 + (yy[msk]-gmy)**2))
        gmyNew = gmyNew / np.sum(1./np.sqrt( (xx[msk]-gmx)**2 + (yy[msk]-gmy)**2))
        resid = np.sqrt( (gmxNew-gmx)**2 + (gmyNew-gmy)**2)
        gmx,gmy=gmxNew,gmyNew
        if resid<=tolerance: break

    return gmx,gmy

def fast_svd(M, k):
    p = k+5
    Y = np.dot(M, np.random.normal(size=(M.shape[1],p)))
    Q,r = linalg.qr(Y)
    B = np.dot(Q.T,M)
    Uhat, s, v = linalg.svd(B, full_matrices=False)
    U = np.dot(Q, Uhat)
    return U.T[:k].T, s[:k], v[:k]

def towav(nim, levels, wavelet):
    coeffs = pywt.wavedec2(nim,wavelet,level=levels)
    cA = coeffs[0]
    nim = cA.flatten()
    cH = [ coeffs[l+1][0] for l in xrange(levels) ]
    cV = [ coeffs[l+1][1] for l in xrange(levels) ]
    cD = [ coeffs[l+1][2] for l in xrange(levels) ]
    for l in xrange(levels):
        nim = np.append(nim, cH[l].flatten())
        nim = np.append(nim, cV[l].flatten())
        nim = np.append(nim, cD[l].flatten())
    return [nim, coeffs, cA, cH, cV, cD]

def fromwav(stack, coeffs, cA, cH, cV, cD, levels, wavelet):
    i1 = len(cA.flatten())
    cA = stack[:i1].reshape(cA.shape)
    coeffs = [cA]
    for l in xrange(levels):
        i2 = i1 + len(cH[l].flatten())
        i3 = i2 + len(cV[l].flatten())
        i4 = i3 + len(cD[l].flatten())
        cHn = stack[i1 :i2 ].reshape(cH[l].shape)
        cVn = stack[i2 :i3 ].reshape(cV[l].shape)
        cDn = stack[i3 :i4 ].reshape(cD[l].shape)
        coeffs.append((cHn,cVn,cDn))
        i1 = i4
    return pywt.waverec2(coeffs,wavelet)

def rank(method, stack, width, height, par):
    if method == 2:
        print "percentile=", par["percentile"]
        p=np.ones((width, height))*par["percentile"]
    else:
        ox,oy = width/2., height/2.
        rmax = np.sqrt(ox**2 + oy**2)
        xx,yy = np.meshgrid(xrange(width),xrange(height))
        w = np.sqrt((xx-ox)**2 + (yy-oy)**2) / rmax
        wmid  = par["spectralMidFrequency"]
        pmid  = par["spectralMidRank"]
        phigh = par["spectralHighRank"]
        print "wmid,pmid,phigh = ", wmid,pmid,phigh
        a2 = (2.*pmid - 1. - 2.*wmid*phigh + wmid) / (2.*wmid*(wmid-1.))
        a1 = (2.*wmid**2*phigh - wmid**2 - 2.*pmid + 1.) / (2.*wmid*(wmid-1.))
        p = 0.5 + a1*w + a2*w**2
        p = np.clip(p,0.0,1.0)
    s = np.sort(stack,axis=0)
    k = (s.shape[0]-1.) * p.flatten()
    f = np.floor(k).astype(int)
    c = np.ceil (k).astype(int)
    d0 = [ s[fi,i] for i,fi in enumerate(f) ] * (c-k)
    d1 = [ s[ci,i] for i,ci in enumerate(c) ] * (k-f)
    stack=d0+d1
    if np.any(f==c):
        stack[f==c] = s[f, f==c]
    return stack

def kappasigma(stack, par):
    print "kappa=", par["kappa"]
    median=np.median(stack, axis=0)
    mean=np.mean(stack, axis=0)
    std =np.std (stack, axis=0)
    out =np.zeros(mean.shape)
    count=np.zeros(mean.shape)
    for i,frame in enumerate(stack):
        mask=np.abs(frame-mean) <= par["kappa"]*std
        out[mask]=out[mask]+frame[mask]
        count[mask]=count[mask]+1.
    count[count==0]=1
    stack=out/count
    return stack

def kappasigmamedian(stack, par):
    print "kappa=", par["kappa"]
    noOfFrames=stack.shape[0]
    median=np.median(stack, axis=0)
    mean=np.mean(stack, axis=0)
    std =np.std (stack, axis=0)
    out =np.zeros(mean.shape)
    for i,frame in enumerate(stack):
        mask=np.abs(frame-mean) <= par["kappa"]*std
        out[mask]=out[mask]+frame[mask]
        mask=np.abs(frame-mean) > par["kappa"]*std
        out[mask]=out[mask]+median[mask]
    stack=out/noOfFrames
    return stack

def svd(stack, par):
    print "noOfSingularValues=", par["noOfSingularValues"]
    U,s,V=fast_svd(stack, par["noOfSingularValues"])
    S = np.diag(s)
    stack = np.dot(U, np.dot(S, V))
    stack = np.median(stack,axis=0)
    return stack

def kalman(stack):
    xhat = np.mean(stack, axis=0)   #A posteriori estimate of x
    P         = np.ones(xhat.shape)   #A posteriori error estimate
    xhatMinus = np.zeros(xhat.shape)  #A priori estimate of x
    Pminus    = np.zeros(xhat.shape)  #A priori error estimate
    K         = np.zeros(xhat.shape)  #Gain or blending factor
    Q = np.std(stack,axis=0) #Process noise
    R = np.var(stack,axis=0) #Estimate of measurement error variance
    for frame in stack:
        xhatMinus[:] = xhat
        Pminus[:] = P + Q
        K[:] = Pminus / (Pminus + R)
        xhat[:] = xhatMinus + K*(frame-xhatMinus)
        P[:] = (1. - K) * Pminus
    stack = xhat
    return stack

def load_stack(im_list, n, dataMode, im_mode, group):
    width = 0
    height = 0
    stack = []
    imw = []
    for xxx in xrange(n):
        print "step ", xxx, "/", n - 1, " group ", group,  " loading ", im_list[xxx]
        im = load_pic(im_list[xxx], im_mode)
        im = [x.astype(myfloat) for x in im]
        width = im[0].shape[0]
        height = im[0].shape[1]
        if dataMode == 1:
            im = [np.fft.rfft2(x) for x in im]
            im = [np.fft.fftshift(x) for x in im]
        elif dataMode == 2:
            levels  = int( np.log2(im[0].shape[0]) )
            #wavelet = pywt.Wavelet('haar')
            wavelet = pywt.Wavelet('bior1.3')
            imw = [towav(x, levels, wavelet) for x in im]
            im = [x[0] for x in imw]
        nim_width = im[0].shape[0]
        #nim_height = im[0].shape[1]
        nim_height = 1
        #Flatten the data and build a stack
        if len(stack)==0:
            stack = [np.array([x.flatten()]) for x in im]
        else:
            stack = map(lambda x: np.append(x[0], np.array([x[1].flatten()]), axis=0), zip(stack,im))
    return (stack, width, height, imw, nim_width, nim_height)

def do_stack(stack, method, para, width, height, dataMode):
    if method == 0:
        stack = [np.mean(x, axis=0) for x in stack]
    elif method == 1:
        stack = [np.median(x, axis=0, overwrite_input=True) for x in stack]
    elif method == 2 or method == 3:
        stack = [rank(method, x, width, height, para) for x in stack]
    elif method == 4:
        stack = [kappasigma(x, para) for x in stack]
    elif method == 5:
        stack = [kappasigmamedian(x, para) for x in stack]
    elif method == 6:
        stack = [svd(x, para) for x in stack]
    elif method == 7:
        stack = [kalman(x) for x in stack]
    else:
        assert False, "Unknown or unapplicable method"
    return stack

def save_stack(stack, width, height, dataMode, im_mode, fname, imw):
    if dataMode == 1:
        stack = [np.reshape(x , (width, height/2 + 1)) for x in stack]
        stack = [np.fft.ifftshift(x) for x in stack]
        stack = [np.fft.irfft2(x) for x in stack]
    elif dataMode == 2:
        levels  = int( np.log2(width) )
        #wavelet = pywt.Wavelet('haar')
        wavelet = pywt.Wavelet('bior1.3')
        stack = [fromwav(x, y[1], y[2], y[3], y[4], y[5], levels, wavelet) for x,y in zip(stack, imw)]
    else:
        stack = [np.reshape(x, (width, height)) for x in stack]
    save_pic(fname, im_mode, stack)
    save_pic(fname + "_gamma", im_mode, gamma_stretch(stack))

def gamma_stretch(im):
    im = [x ** gamma for x in im]
    mi = min([x.min() for x in im])
    ma = max([x.max() for x in im])
    k = 65535.0 / (ma - mi)
    im = [ (x - mi) * k for x in im]
    return im

def load_pic(fname, im_mode):
    fname = os.path.splitext(fname)[0]
    if im_mode == 0 or im_mode == 3 or im_mode == 5:
        im = read_ppm(fname + ".ppm")
    elif im_mode == 1  or im_mode == 4 or im_mode == 6 or im_mode == 7 or im_mode == 8:
        im = [read_pgm(fname + ".pgm")]
    elif im_mode == 2 :
        im = read_ppm(fname + ".ppm")
        im = [0.299 * im[0] + 0.587 * im[1] + 0.114 * im[2]]
    else:
        assert False, "Unknown image mode"
    if im_mode == 5 or im_mode == 6:
        im = [x * 256 for x in im]
    return im

def save_pic(fname, im_mode, im):
    fname = os.path.splitext(fname)[0]
    im = [x.clip(0, 65535) for x in im]
    if im_mode == 0 or im_mode == 5 or im_mode == 7:
        write_ppm_65535(fname + ".ppm", im)
    elif im_mode == 1 or im_mode == 2 or im_mode == 6:
        write_pgm_65535(fname + ".pgm", im[0])
    elif im_mode == 3 or im_mode == 8:
        write_ppm_255(fname + ".ppm", im)
    elif im_mode == 4:
        write_pgm_255(fname + ".pgm", im[0])
    else:
        assert False, "Unknown image mode"

def expand_args(args):
    r = []
    for i in args:
        if i[0] == '@':
            with open(i[1:]) as fp:
                for line in fp:
                    r.append(line)    
        else:
            r.append(i)
    return r

(DEBAYER_NONE, DEBAYER_BGGR, DEBAYER_GRBG) = range(3)
(DEBAYER_VECTOR_MEDIAN, DEBAYER_VECTOR_MEAN) = range(2)
def demosaic(nim,pattern,method=DEBAYER_VECTOR_MEDIAN):
    print "Demosaic:",nim.shape
    if len(nim.shape)==3:
        nim = np.mean(nim, axis=2)

    xs,ys = nim.shape[0],nim.shape[1]
    xr,yr = np.arange(xs),np.arange(ys)
    xx,yy = np.meshgrid(yr,xr)
    red   = np.zeros(nim.shape)
    green = np.zeros(nim.shape)
    blue  = np.zeros(nim.shape)
    
    nimXP = np.roll(nim, 1,axis=0)
    nimXM = np.roll(nim,-1,axis=0)
    nimYP = np.roll(nim, 1,axis=1)
    nimYM = np.roll(nim,-1,axis=1)
    nimXPYP = np.roll(nimXP, 1,axis=1)
    nimXMYP = np.roll(nimXM, 1,axis=1)
    nimXPYM = np.roll(nimXP,-1,axis=1)
    nimXMYM = np.roll(nimXM,-1,axis=1)
    nx, ny = nim.shape[0], nim.shape[1]
    xx, yy = np.mgrid[0:nx,0:ny]
    
    #Building pixel masks
    if pattern==DEBAYER_GRBG:
        G1mask = ((xx % 2)==0) & ((yy % 2)==0)
        Rmask  = ((xx % 2)==1) & ((yy % 2)==0)
        Bmask  = ((xx % 2)==0) & ((yy % 2)==1)
        G2mask = ((xx % 2)==1) & ((yy % 2)==1)
    elif pattern==DEBAYER_BGGR:
        G1mask = ((xx % 2)==0) & ((yy % 2)==1)
        Rmask  = ((xx % 2)==1) & ((yy % 2)==1)
        Bmask  = ((xx % 2)==0) & ((yy % 2)==0)
        G2mask = ((xx % 2)==1) & ((yy % 2)==0)
    
    if pattern==DEBAYER_GRBG or pattern==DEBAYER_BGGR:
        G1 = np.zeros([3,nim.shape[0],nim.shape[1]])
        G2 = np.zeros([3,nim.shape[0],nim.shape[1]])
        R  = np.zeros([3,nim.shape[0],nim.shape[1]])
        B  = np.zeros([3,nim.shape[0],nim.shape[1]])
        
        #Building virtual pixels
        G1 = [[nimXM, nim    , nimYP],
              [nimXP, nim    , nimYP],
              [nimXM, nim    , nimYM],
              [nimXP, nim    , nimYM]]
        
        G2 = [[nimYP, nim    , nimXM],
              [nimYP, nim    , nimXP],
              [nimYM, nim    , nimXM],
              [nimYM, nim    , nimXP]]

        R = [[nim, nimXM, nimXMYM],
             [nim, nimYM, nimXMYM],
             [nim, nimXP, nimXPYM],
             [nim, nimYM, nimXPYM],
             [nim, nimXM, nimXMYP],
             [nim, nimYP, nimXMYP],
             [nim, nimXP, nimXPYP],
             [nim, nimYP, nimXPYP]]

        B = [[nimXMYM, nimXM, nim],
             [nimXMYM, nimYM, nim],
             [nimXPYM, nimXP, nim],
             [nimXPYM, nimYM, nim],
             [nimXMYP, nimXM, nim],
             [nimXMYP, nimYP, nim],
             [nimXPYP, nimXP, nim],
             [nimXPYP, nimYP, nim]]
        
        #Calculate the real pixel values from the median of the virtual pixels 
        if method==DEBAYER_VECTOR_MEDIAN:
            G1 = np.median(G1,axis=0)
            G2 = np.median(G2,axis=0)
            R  = np.median(R ,axis=0)
            B  = np.median(B ,axis=0)
        #Calculate the real pixel values from the mean of the virtual pixels (faster but more susceptible to noise)
        elif method==DEBAYER_VECTOR_MEAN:
            G1 = np.mean(G1,axis=0)
            G2 = np.mean(G2,axis=0)
            R  = np.mean(R ,axis=0)
            B  = np.mean(B ,axis=0)
            
        #Form the final image channels
        red   = np.array(nim)
        green = np.array(nim)
        blue  = np.array(nim)
        red  [G1mask] = G1[0][G1mask]
        red  [Rmask]  = R [0][Rmask ]
        red  [Bmask]  = B [0][Bmask ]
        red  [G2mask] = G2[0][G2mask]
        green[G1mask] = G1[1][G1mask]
        green[Rmask]  = R [1][Rmask ]
        green[Bmask]  = B [1][Bmask ]
        green[G2mask] = G2[1][G2mask]
        blue [G1mask] = G1[2][G1mask]
        blue [Rmask]  = R [2][Rmask ]
        blue [Bmask]  = B [2][Bmask ]
        blue [G2mask] = G2[2][G2mask]
        
    return [red, green, blue]

SER_HEADER = "<14siiiiiii40s40s40sqq"

class SerWriter:

    def __init__(self, out_file, shape, channels, observer="", instrument="", telescope=""):
        self.out_file = out_file
        if out_file == "-":
            self.fd = sys.stdout
        else:
            self.fd = open(out_file, "w")
        self.w = shape[0]
        self.h = shape[1]
        self.channels = channels
        self.count = 0
        self.observer = observer
        self.instrument = instrument
        self.telescope = telescope
        self.write_header()
        
    def write_header(self):
        if self.channels == 1:
            color_id = 0
        else:
            color_id = 100
        hdr = struct.pack(SER_HEADER,
                          "LUCAM-RECORDER",
                          0,
                          color_id,
                          0,
                          self.w,
                          self.h,
                          16,
                          self.count,
                          self.observer,
                          self.instrument,
                          self.telescope,
                          0,
                          0)
        self.fd.write(hdr)

    def write_frame(self, frame):
        self.count += 1
        frame = [x.clip(0, 65535) for x in frame]
        frame = [x.astype(np.uint16) for x in frame]
        if self.channels == 1:
            frame = frame[0].transpose((1,0))
        else:
            frame = np.dstack(frame)
            frame = frame.transpose((1,0,2))
        frame.tofile(self.fd)

    def close(self):
        if self.out_file != "-":
            self.fd.seek(0, 0)
            self.write_header()
            self.fd.close()

            
class SerReader:

    def __init__(self, in_file, raw=False):
        self.raw = raw
        if in_file == "-":
            self.fd = sys.stdout
        else:
            self.fd = open(in_file, "r")
        hdr = self.fd.read(178)
        (id, lu_id, self.color_id, end, self.width, self.height, self.depth, self.count,
         obsrver, instrument, telescope, dt, dt_utc) = struct.unpack(SER_HEADER, hdr)

    def get(self):
        if self.color_id == 9 or self.color_id == 0:
            if self.depth == 16:
                buffer = self.fd.read(2 * self.width * self.height)
                r = np.frombuffer(buffer,
                            dtype='<u2',
                            count=int(self.width)*int(self.height),
                            offset=0
                            ).reshape((self.height, self.width))
            else:
                buffer = self.fd.read(self.width * self.height)
                r = np.frombuffer(buffer,
                            dtype='u1',
                            count=int(self.width)*int(self.height),
                            offset=0
                            ).reshape((self.height, self.width))
            r = r.transpose()
            if self.raw or self.color_id == 0:
                return r
            return demosaic(r, 2, 0)
        elif self.color_id == 101:
            if self.depth == 8:
                buffer = self.fd.read(3 * self.width * self.height)
                arr = np.frombuffer(buffer,
                            dtype='u1',
                            count=int(self.width)*int(self.height)*3,
                            offset=0
                            ).reshape((self.height, self.width, 3))
                r = [arr[:,:,2], arr[:,:,1], arr[:,:,0]]
                return [x.transpose() for x in r]
        raise ValueError("Unsupported color_is %d depth %d" % self.color_id, self.depth)


