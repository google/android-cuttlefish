import {Component, OnInit, OnDestroy, AfterViewInit, ViewChild, Inject, ElementRef} from '@angular/core';
import {DisplaysService} from '../displays.service';
import {BehaviorSubject, debounceTime, filter, fromEvent, merge, range, Subscription} from 'rxjs';
import {KtdDragEnd, KtdDragStart, KtdGridCompactType, KtdGridComponent, KtdGridLayout, KtdGridLayoutItem, KtdResizeEnd, KtdResizeStart, ktdTrackById} from '@katoid/angular-grid-layout';
import {DOCUMENT} from '@angular/common';
import {Device} from '../device-interface';

@Component({
  selector: 'app-view-pane',
  templateUrl: './view-pane.component.html',
  styleUrls: ['./view-pane.component.sass'],
})
export class ViewPaneComponent implements OnInit, OnDestroy, AfterViewInit {
  @ViewChild(KtdGridComponent, {static: true}) grid!: KtdGridComponent;
  @ViewChild('viewPane', {static: true}) viewPane!: ElementRef;
  trackById = ktdTrackById;

  visibleDevices = this.displaysService.getVisibleDevices();

  cols$ = new BehaviorSubject<number>(0);
  cols : number = 0;
  rowHeight : number = 0;
  compactType: KtdGridCompactType = 'horizontal';
  layout: KtdGridLayout = [];
  createdDeviceLayout: KtdGridLayout = [];
  currentTransition: string = 'transform 500ms ease, width 500ms ease, height 500ms ease';

  dragStartThreshold = 0;
  autoScroll = true;
  disableDrag = false;
  disableResize = false;
  disableRemove = false;
  autoResize = true;
  preventCollision = false;
  isDragging = false;
  isResizing = false;
  resizeSubscription!: Subscription;
  visibleSubscription!: Subscription;
  observer! : ResizeObserver;

  constructor(public displaysService: DisplaysService, @Inject(DOCUMENT) public document: Document) {

  }

  ngOnInit(): void {
    this.visibleSubscription = this.visibleDevices.subscribe((allDisplays: Device[]) => {
      this.layout = this.layout.filter((ktdGridItem: KtdGridLayoutItem) => allDisplays.some(aDisplay => aDisplay.deviceId == ktdGridItem.id));
      allDisplays.filter((aDisplay: Device) => !this.layout.some((ktdGridItem: KtdGridLayoutItem) => aDisplay.deviceId == ktdGridItem.id))
      .forEach((addDisplay: Device) => this.addToLayout(addDisplay.deviceId));
      this.cols$.subscribe((num) => this.cols = num);
      this.rowHeight = 1;
      this.observer = new ResizeObserver(entries => {
        this.cols$.next(entries[0].contentRect.width);
      })
      this.observer.observe(this.viewPane.nativeElement);
    });
  }

  ngAfterViewInit(): void {
    this.resizeSubscription = merge(fromEvent(window, 'resize'), fromEvent(window, 'orientationchange'))
    .pipe(debounceTime(50), filter(() => this.autoResize)
    ).subscribe(() => {
      this.grid.resize();
    })
  }

  ngOnDestroy(): void {
    this.resizeSubscription.unsubscribe();
    this.visibleSubscription.unsubscribe();
    this.observer.unobserve(this.viewPane.nativeElement);
  }

  onDragStarted(event: KtdDragStart) {
    this.isDragging = true;
  }

  onResizeStarted(event: KtdResizeStart) {
    this.isResizing = true;
  }

  onDragEnded(event: KtdDragEnd) {
    this.isDragging = false;
  }

  onResizeEnded(event: KtdResizeEnd) {
    this.isResizing = false;
  }

  onLayoutUpdated(layout: KtdGridLayout) {
    this.layout = layout;
    this.createdDeviceLayout = this.createdDeviceLayout.filter(obj => !layout.some(element => element.id == obj.id));
    layout.forEach(devices => {
      this.createdDeviceLayout.push(devices);
    });
  }

  addToLayout(display: string) {
    if (!this.createdDeviceLayout.some(element => element.id == display)) {
      this.addDeviceToLayout(display);
    }
    this.layout.push(this.createdDeviceLayout.find(obj => obj.id == display)!);
  }

  addDeviceToLayout(display: string) {
    const newItemWidth = 720;
    const newItemHeight = 1250;
    const lastMaxY = (this.layout.length != 0)? Math.max(...this.layout.map(obj => obj.y)) : 0;
    const lastRowLayout = this.layout.filter(obj => obj.y == lastMaxY);
    const lastMaxX = (this.layout.length != 0)? Math.max(...lastRowLayout.map(obj => obj.x)) : -newItemWidth;
    const newLayoutItem: KtdGridLayoutItem = {
      id: display,
      x: (lastMaxX + (newItemWidth*2) <= this.cols)? (lastMaxX + newItemWidth) : 0,
      y: (lastMaxX + (newItemWidth*2) <= this.cols)? lastMaxY : lastMaxY + newItemHeight,
      w: newItemWidth,
      h: newItemHeight
    };

    this.createdDeviceLayout.push(newLayoutItem);
  }

}
