/* Enhance the ordered list; leave all content available without JavaScript. */
document.querySelectorAll('.manifesto-carousel').forEach(carousel => {
    const track = carousel.querySelector('.carousel-track');
    if (track.classList.contains('carousel-ready')) return;
    const slides = [...track.children];
    if (slides.length < 2) return;
    const controls = carousel.querySelector('.carousel-controls');
    const previous = controls.querySelector('[data-step="-1"]');
    const next = controls.querySelector('[data-step="1"]');
    const status = controls.querySelector('.carousel-status');
    let current = 0;
    track.classList.add('carousel-ready');
    controls.hidden = false;
    function update(index) {
        current = index;
        status.textContent = `${index + 1} / ${slides.length}`;
        previous.disabled = index === 0;
        next.disabled = index === slides.length - 1;
        slides.forEach((slide, position) => {
            if (position !== index) slide.querySelectorAll('video').forEach(video => video.pause());
        });
    }
    function go(index) {
        index = Math.max(0, Math.min(index, slides.length - 1));
        update(index);
        track.scrollTo({left: slides[index].offsetLeft - slides[0].offsetLeft,
                        behavior: 'instant'});
    }
    controls.querySelectorAll('button').forEach(button => {
        button.addEventListener('click', () => go(current + Number(button.dataset.step)));
    });
    track.addEventListener('keydown', event => {
        if (event.target !== track) return;
        const indexes = {ArrowLeft: current - 1, ArrowRight: current + 1,
                         Home: 0, End: slides.length - 1};
        if (!(event.key in indexes)) return;
        event.preventDefault();
        go(indexes[event.key]);
    });
    track.addEventListener('scroll', () => {
        const left = track.getBoundingClientRect().left;
        const distances = slides.map(slide => Math.abs(slide.getBoundingClientRect().left - left));
        const index = distances.indexOf(Math.min(...distances));
        update(index);
    }, {passive: true});
    new ResizeObserver(() => go(current)).observe(track);
    update(0);
});
